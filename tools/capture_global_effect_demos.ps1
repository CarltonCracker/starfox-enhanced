param(
    [string]$Binary='build/current/starfox_pc.exe',
    [string]$OutputDirectory='build/validation/active/global-effect-demos',
    [string[]]$Effects=@('Light Shafts'),
    [ValidateRange(1,12)][int]$Seconds=6,
    [ValidateSet('direct3d12','vulkan')][string]$GpuDriver='direct3d12',
    [switch]$All,
    [switch]$List
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskOutput=[IO.Path]::GetFullPath((Join-Path $taskRoot $OutputDirectory))
$taskValidation=[IO.Path]::GetFullPath((Join-Path $taskRoot 'build/validation'))
if(!$taskOutput.StartsWith($taskValidation+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Demo scratch must remain under build/validation on C:'
}
$taskCatalogue=@(
    'Light Shafts','Anamorphic Flare','Halation','Soft Focus','Radial Blur',
    'Heat Haze','Sharpen','Vignette','Film Grain','CRT Scanlines',
    'Phosphor Mask','CRT Curvature','Lens Ghosts'
)
if($List){$taskCatalogue;exit 0}
if($All){$Effects=$taskCatalogue}
foreach($effect in $Effects){if($effect -notin $taskCatalogue){throw "Unknown effect: $effect"}}
$taskBinary=[IO.Path]::GetFullPath((Join-Path $taskRoot $Binary))
$taskFfmpeg=(Get-Command ffmpeg -ErrorAction Stop).Source
$taskRom=Join-Path $taskRoot 'upstream-ultrastarfox/SF.SFC'
$taskSymbols=Join-Path $taskRoot 'upstream-ultrastarfox/SYMBOLS.TXT'
foreach($path in @($taskBinary,$taskRom,$taskSymbols)){if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing input: $path"}}
if(Test-Path -LiteralPath $taskOutput){throw 'Preserve existing demos; choose a new output directory'}
# Each side uses a native 4:3 capture (256x224). Keep one baseline and only
# one enabled effect's frames. The final 30-FPS video contains no stacked FX.
$taskFrames=$Seconds*60
$taskVideoFrames=$Seconds*30
$taskBudget=[long]($taskVideoFrames*256*224*4*2+67108864)
[void](& (Join-Path $taskValidation 'check-storage.ps1') -PlannedBytes $taskBudget)
New-Item -ItemType Directory -Path $taskOutput | Out-Null
$taskSaved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -match '^(STARFOX_|SDL_AUDIODRIVER$|SDL_GPU_DRIVER$)'} | ForEach-Object {
    $taskSaved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+ $_.Name)
}
$taskReceipt=[ordered]@{binary_sha256=(Get-FileHash -LiteralPath $taskBinary).Hash;fps=30;seconds=$Seconds;
    level='LEVEL1_1';renderer='GPU';backend=$GpuDriver;quality='HIGH';effects=@();source='actual_gameplay_captures'}
function Save-DemoReceipt {$taskReceipt|ConvertTo-Json -Depth 6|Out-File -LiteralPath (Join-Path $taskOutput 'demos.json') -Encoding utf8}
function Capture-Side([string]$Name,[uint32]$Packed){
    $directory=Join-Path $taskOutput $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    $env:STARFOX_TEST_GLOBAL_ENHANCEMENTS="$Packed"
    $env:STARFOX_CAPTURE_PRESENTATION_PATH=Join-Path $directory 'capture.bmp'
    $process=Start-Process -FilePath $taskBinary -WorkingDirectory $taskRoot -ArgumentList @(
        ('"'+$taskRom+'"'),('"'+$taskSymbols+'"'),'LEVEL1_1') -WindowStyle Hidden -PassThru `
        -RedirectStandardError (Join-Path $directory 'runtime.log')
    $null=$process.Handle
    $taskReceipt.active_pid=$process.Id;Save-DemoReceipt
    $deadline=[datetime]::UtcNow.AddMinutes(10)
    while(!$process.WaitForExit(500)){
        [void](& (Join-Path $taskValidation 'check-storage.ps1'))
        if([datetime]::UtcNow -gt $deadline){throw "Capture is still running (PID $($process.Id)); inspect it before retrying"}
    }
    $taskReceipt.active_pid=$null
    if($process.ExitCode -ne 0){throw "Gameplay capture failed ($($process.ExitCode)): $directory/runtime.log"}
    $frames=@(Get-ChildItem -LiteralPath $directory -File -Filter 'capture.bmp.frame-*.bmp' |
        Sort-Object {[int]($_.BaseName -replace '^capture\.bmp\.frame-','')})
    if($frames.Count -ne $taskVideoFrames){throw "Expected $taskVideoFrames actual final-presentation frames, found $($frames.Count)"}
    $concat=@('ffconcat version 1.0')
    foreach($frame in $frames){
        $concat+="file '"+$frame.FullName.Replace('\','/').Replace("'","'\''")+"'"
        $concat+='duration 0.0333333333333333'
    }
    $concat+="file '"+$frames[-1].FullName.Replace('\','/').Replace("'","'\''")+"'"
    $listPath=Join-Path $directory 'frames.ffconcat'
    $concat|Out-File -LiteralPath $listPath -Encoding ascii
    return [pscustomobject]@{directory=$directory;frames=$frames;concat=$listPath}
}
function Remove-OwnedFrames($Side){
    # Only individually enumerated BMPs made by this invocation are removed;
    # never recurse into a user directory or delete settings/saves/private ROMs.
    foreach($frame in $Side.frames){
        $path=[IO.Path]::GetFullPath($frame.FullName)
        if(!$path.StartsWith($taskOutput+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe frame cleanup path'}
        Remove-Item -LiteralPath $path
    }
    $last=Join-Path $Side.directory 'capture.bmp'
    if(Test-Path -LiteralPath $last){Remove-Item -LiteralPath $last}
}
try {
    $settings=@{
        SDL_AUDIODRIVER='dummy';SDL_GPU_DRIVER=$GpuDriver;STARFOX_TEST_HIDDEN='1'
        STARFOX_TEST_FRAMES="$taskFrames";STARFOX_TEST_SKIP_PREROLL='1';STARFOX_TEST_PREROLL_TICKS='1000'
        STARFOX_TEST_UNPACED='1';STARFOX_TEST_VSYNC='0';STARFOX_TEST_PRESENTATION_FPS='60'
        STARFOX_TEST_TIMING_MODE='ORIGINAL';STARFOX_TEST_EXPERIENCE='ORIGINAL';STARFOX_TEST_GOD_MODE='1'
        STARFOX_TEST_DISPLAY_MODE='4_3';STARFOX_TEST_RENDER_SCALE='1';STARFOX_TEST_RENDERER='GPU'
        STARFOX_TEST_EFFECT='0';STARFOX_TEST_MATERIAL='0';STARFOX_TEST_MANIPULATION='0'
        STARFOX_TEST_WORLD_DISTORTION='0';STARFOX_TEST_MODEL_FX='0';STARFOX_TEST_WORLD_FX='0'
        STARFOX_TEST_WORLD_EFFECT='0';STARFOX_TEST_RAY_TRACING='0';STARFOX_TEST_REFLECTIVE_SURFACES='0'
        STARFOX_TEST_RTX_LIGHTING='0';STARFOX_TEST_BLOOM='0';STARFOX_TEST_BLOOM_2D='0'
        STARFOX_TEST_2D_FILTER='0';STARFOX_TEST_ANTI_ALIASING='0';STARFOX_TEST_HDR_EFFECT='0'
        STARFOX_TEST_CHROMATIC_ABERRATION='0';STARFOX_TEST_MODEL_SMOOTHING='0'
        STARFOX_TEST_FSR1_SELECTION='0';STARFOX_TEST_DLSS_SELECTION='0';STARFOX_TEST_DLSS45_SELECTION='0'
        STARFOX_TEST_STEREO_OUTPUT='0';STARFOX_TEST_SHOW_FPS='0';STARFOX_TEST_MSU1='0'
        STARFOX_TEST_SOFTWARE_SHADOWS='0';STARFOX_TEST_SCENE_ENHANCEMENTS='0';STARFOX_TEST_DEPTH_ENHANCEMENTS='0'
        STARFOX_TEST_PARTICLE_ENHANCEMENTS='0';STARFOX_TEST_PHOSPHOR_PERSISTENCE='0';STARFOX_TEST_ADAPTIVE_EXPOSURE='0'
        STARFOX_TEST_WATER_CAUSTICS='0';STARFOX_TEST_SHADOW_SOFTNESS='0';STARFOX_TEST_CAMERA_RESPONSE='0'
        STARFOX_TEST_VOLUMETRIC_FOG_QUALITY='0';STARFOX_TEST_MOTION_BLUR_QUALITY='0'
        STARFOX_TEST_ENVIRONMENT_0='0';STARFOX_TEST_ENVIRONMENT_3='0'
        STARFOX_CAPTURE_PRESENTATION_SEQUENCE='1';STARFOX_CAPTURE_PRESENTATION_INTERVAL='2'
        STARFOX_CAPTURE_PRESENTATION_FIRST='1';STARFOX_CAPTURE_PRESENTATION_LAST="$taskFrames"
        STARFOX_TEST_PRESSES='30:0x8000,60:0x8000,90:0x8000,120:0x0080,150:0x8000,180:0x8000,210:0x8000,240:0x8000,270:0x8000,300:0x8000'
    }
    foreach($key in $settings.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $settings[$key]}
    Save-DemoReceipt
    Write-Output 'Capturing the clean gameplay baseline'
    $baseline=Capture-Side 'off' 0
    foreach($effect in $Effects){
        [void](& (Join-Path $taskValidation 'check-storage.ps1') -PlannedBytes ([long]($taskVideoFrames*256*224*4+8388608)))
        $index=[array]::IndexOf($taskCatalogue,$effect)
        $slug=$effect.ToLowerInvariant().Replace(' ','-')
        $packed=[uint32](3 -shl ($index*2))
        Write-Output "Capturing $effect only (HIGH)"
        $enabled=Capture-Side $slug $packed
        $changed=0
        for($i=0;$i -lt $baseline.frames.Count;$i++){
            if((Get-FileHash -LiteralPath $baseline.frames[$i].FullName).Hash -cne (Get-FileHash -LiteralPath $enabled.frames[$i].FullName).Hash){$changed++}
        }
        if(!$changed){throw "No visible frame differences for $effect; do not publish a misleading demonstration"}
        $video=Join-Path $taskOutput ($slug+'.mp4')
        $filter="[0:v]scale=640:480:flags=neighbor,pad=640:530:0:50:color=0x14202c,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='OFF':fontsize=22:fontcolor=white:x=20:y=14[left];"+
            "[1:v]scale=640:480:flags=neighbor,pad=640:530:0:50:color=0x14202c,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='$effect - HIGH':fontsize=22:fontcolor=white:x=20:y=14[right];[left][right]hstack=inputs=2[out]"
        & $taskFfmpeg -hide_banner -loglevel error -nostdin -f concat -safe 0 -i $baseline.concat `
            -f concat -safe 0 -i $enabled.concat -filter_complex $filter -map '[out]' `
            -frames:v $taskVideoFrames -r 30 -c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p -movflags +faststart $video
        if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $video)){throw 'Video encoding failed'}
        $taskReceipt.effects+=@{name=$effect;packed=$packed;changed_frames=$changed;video=[IO.Path]::GetFileName($video);sha256=(Get-FileHash -LiteralPath $video).Hash}
        Save-DemoReceipt
        Remove-OwnedFrames $enabled
        Write-Output "Ready: $video"
    }
    Remove-OwnedFrames $baseline
    $taskReceipt.complete=$true;Save-DemoReceipt
} finally {
    Get-ChildItem Env: | Where-Object {$_.Name -match '^(STARFOX_|SDL_AUDIODRIVER$|SDL_GPU_DRIVER$)'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $taskSaved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $taskSaved[$key]}
}
