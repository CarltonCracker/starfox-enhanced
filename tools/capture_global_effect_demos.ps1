param(
    [string]$Binary='build/current/starfox_pc.exe',
    [string]$OutputDirectory='build/validation/active/global-effect-demos',
    [string[]]$Effects=@('Light Shafts'),
    [ValidateRange(1,12)][int]$Seconds=6,
    [ValidateSet('direct3d12','vulkan')][string]$GpuDriver='direct3d12',
    [switch]$All,
    [switch]$Additional,
    [switch]$TracePoints,
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
$taskImageSlots=@(
    'Light Shafts','Anamorphic Flare','Halation','','Radial Blur',
    'Heat Haze','Sharpen','Vignette','Film Grain','CRT Scanlines',
    '','CRT Curvature','Lens Ghosts'
)
$taskImageEffects=@($taskImageSlots|Where-Object {$_})
$taskSpecs=@{}
for($i=0;$i -lt $taskImageSlots.Count;$i++){
    if(!$taskImageSlots[$i]){continue}
    $taskSpecs[$taskImageSlots[$i]]=@{key='STARFOX_TEST_GLOBAL_ENHANCEMENTS';value=[uint32](3 -shl ($i*2));context=@{};stage='LEVEL1_1';left='OFF'}
}
function Add-DemoSpec([string]$Name,[string]$Key,[int]$Value,[hashtable]$Context=@{},[string]$Stage='LEVEL1_1',[string]$Left='OFF'){
    $taskSpecs[$Name]=@{key=$Key;value=$Value;context=$Context;stage=$Stage;left=$Left}
}
Add-DemoSpec 'Volumetric Fog' 'STARFOX_TEST_VOLUMETRIC_FOG_QUALITY' 3
Add-DemoSpec 'Motion Blur' 'STARFOX_TEST_MOTION_BLUR_QUALITY' 3
Add-DemoSpec 'Enhanced Lighting' 'STARFOX_TEST_RTX_LIGHTING' 3
Add-DemoSpec 'Ambient Occlusion' 'STARFOX_TEST_DEPTH_ENHANCEMENTS' 3
Add-DemoSpec 'Depth of Field' 'STARFOX_TEST_DEPTH_ENHANCEMENTS' 12
Add-DemoSpec 'Contrast' 'STARFOX_TEST_HDR_EFFECT' 3
Add-DemoSpec 'Adaptive Exposure' 'STARFOX_TEST_ADAPTIVE_EXPOSURE' 3 @{STARFOX_TEST_EXPERIENCE='EX'} 'LEVEL6_4'
Add-DemoSpec 'Chromatic Aberration' 'STARFOX_TEST_CHROMATIC_ABERRATION' 3
Add-DemoSpec 'Impact Shake' 'STARFOX_TEST_CAMERA_RESPONSE' 3 @{STARFOX_TEST_GOD_MODE='0';STARFOX_TEST_PREROLL_TICKS='60';STARFOX_TEST_PRESSES=''} 'LEVEL1_2'
Add-DemoSpec 'Weapon Recoil' 'STARFOX_TEST_CAMERA_RESPONSE' 12 @{STARFOX_TEST_PRESSES='30:0x4000,60:0x4000,90:0x4000,120:0x4000,150:0x4000,180:0x4000,210:0x4000,240:0x4000,270:0x4000,300:0x4000'}
Add-DemoSpec 'Explosion Shockwaves' 'STARFOX_TEST_SCENE_ENHANCEMENTS' 3
Add-DemoSpec 'Weapon Lighting' 'STARFOX_TEST_SCENE_ENHANCEMENTS' 12 @{STARFOX_TEST_EXPERIENCE='EX';STARFOX_TEST_PRESSES='20:0x4000,90:0x4000,160:0x4000,230:0x4000,300:0x4000';STARFOX_TEST_PRESS_FRAMES='30'} 'LEVEL6_4'
Add-DemoSpec 'Impact Sparks and Debris' 'STARFOX_TEST_PARTICLE_ENHANCEMENTS' 3
Add-DemoSpec 'Exhaust Trails' 'STARFOX_TEST_SCENE_ENHANCEMENTS' 48
Add-DemoSpec 'Exhaust Heat Distortion' 'STARFOX_TEST_PARTICLE_ENHANCEMENTS' 12 @{STARFOX_TEST_PRESSES='20:0x0800,140:0x0400,260:0x0800';STARFOX_TEST_PRESS_FRAMES='80'}
Add-DemoSpec 'CRT Phosphor Persistence' 'STARFOX_TEST_PHOSPHOR_PERSISTENCE' 3
Add-DemoSpec 'Water Caustics' 'STARFOX_TEST_WATER_CAUSTICS' 3 @{STARFOX_TEST_ENVIRONMENT_0='1';STARFOX_TEST_ENVIRONMENT_1='5';STARFOX_TEST_RAY_TRACING='1';STARFOX_TEST_RT_QUALITY='3'}
Add-DemoSpec 'Ray Tracing' 'STARFOX_TEST_RT_QUALITY' 3 @{STARFOX_TEST_ENVIRONMENT_0='1';STARFOX_TEST_ENVIRONMENT_1='1'}
Add-DemoSpec 'Shadow Softness' 'STARFOX_TEST_SHADOW_SOFTNESS' 3 @{STARFOX_TEST_RAY_TRACING='1';STARFOX_TEST_RT_QUALITY='3'} 'LEVEL1_1' 'HARD'
Add-DemoSpec 'Reflective Surfaces' 'STARFOX_TEST_REFLECTIVE_SURFACES' 3 @{STARFOX_TEST_RAY_TRACING='1';STARFOX_TEST_RT_QUALITY='3';STARFOX_TEST_ENVIRONMENT_0='1';STARFOX_TEST_ENVIRONMENT_1='6'}
$taskNotes=@{
    'Halation'='Warm glow around bright cloud and model edges'
    'Heat Haze'='Watch the clouds and mountain contours bend'
    'Adaptive Exposure'='Night-stage scene adapts; HUD remains unchanged'
    'Weapon Recoil'='Actual laser firing triggers a camera kick and recovery'
    'Impact Shake'='Actual shield damage jolts the world; HUD stays still'
    'Weapon Lighting'='Laser light affects nearby polygon surfaces'
    'Exhaust Heat Distortion'='Watch textured scenery behind the moving ship'
}
$taskAdditionalEffects=@($taskSpecs.Keys|Where-Object {$_ -notin $taskImageEffects}|Sort-Object)
$taskCatalogue=@($taskImageEffects)+@($taskAdditionalEffects)
if($List){$taskCatalogue;exit 0}
if($All){$Effects=$taskCatalogue}elseif($Additional){$Effects=$taskAdditionalEffects}
foreach($effect in $Effects){if($effect -notin $taskCatalogue){throw "Unknown effect: $effect"}}
$taskBinary=[IO.Path]::GetFullPath((Join-Path $taskRoot $Binary))
$taskFfmpeg=(Get-Command ffmpeg -ErrorAction Stop).Source
$taskRom=Join-Path $taskRoot 'upstream-ultrastarfox/SF.SFC'
$taskSymbols=Join-Path $taskRoot 'assets/symbols/ultrastarfox.txt'
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
function Capture-Side([string]$Name,[hashtable]$Overrides,[string]$Stage){
    $directory=Join-Path $taskOutput $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    foreach($key in $settings.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $settings[$key]}
    foreach($key in $Overrides.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $Overrides[$key]}
    $env:STARFOX_CAPTURE_PRESENTATION_PATH=Join-Path $directory 'capture.bmp'
    $rom=$taskRom;$symbols=$taskSymbols
    if($env:STARFOX_TEST_EXPERIENCE -eq 'EX'){
        $rom=Join-Path $taskRoot 'tmp/runtime-inputs/starfox-ex/SFES.SFC'
        $symbols=Join-Path $taskRoot 'assets/symbols/starfox-ex.txt'
    }
    $process=Start-Process -FilePath $taskBinary -WorkingDirectory $taskRoot -ArgumentList @(
        ('"'+$rom+'"'),('"'+$symbols+'"'),$Stage) -WindowStyle Hidden -PassThru `
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
        STARFOX_TEST_RT_QUALITY='0';STARFOX_TEST_GLOBAL_ENHANCEMENTS='0';STARFOX_TEST_PRESS_FRAMES='3'
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
        STARFOX_TRACE_SCENE_FX='1';STARFOX_TRACE_GPU='1'
        STARFOX_TEST_REQUIRE_CLEAN_RUNTIME='1'
        STARFOX_CAPTURE_PRESENTATION_SEQUENCE='1';STARFOX_CAPTURE_PRESENTATION_INTERVAL='2'
        STARFOX_CAPTURE_PRESENTATION_FIRST='1';STARFOX_CAPTURE_PRESENTATION_LAST="$taskFrames"
        STARFOX_TEST_PRESSES='30:0x8000,60:0x8000,90:0x8000,120:0x0080,150:0x8000,180:0x8000,210:0x8000,240:0x8000,270:0x8000,300:0x8000'
    }
    if($TracePoints){$settings.STARFOX_TRACE_SCENE_FX_POINTS='1'}
    foreach($key in $settings.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $settings[$key]}
    Save-DemoReceipt
    $baseline=$null;$baselineKey='';$baselineIndex=0
    foreach($effect in $Effects){
        $spec=$taskSpecs[$effect]
        $contextKey=$spec.stage+'|'+($spec.context|ConvertTo-Json -Compress)
        if($contextKey -cne $baselineKey){
            if($null -ne $baseline){Remove-OwnedFrames $baseline}
            $baselineIndex++
            Write-Output ('Capturing clean gameplay baseline '+$baselineIndex+' ('+$spec.stage+')')
            $baseline=Capture-Side ('off-'+$baselineIndex) $spec.context $spec.stage
            $baselineKey=$contextKey
        }
        [void](& (Join-Path $taskValidation 'check-storage.ps1') -PlannedBytes ([long]($taskVideoFrames*256*224*4+8388608)))
        $slug=$effect.ToLowerInvariant().Replace(' ','-')
        Write-Output "Capturing $effect only (HIGH)"
        $overrides=@{}+$spec.context
        $overrides[$spec.key]="$($spec.value)"
        $enabled=Capture-Side $slug $overrides $spec.stage
        $changed=0
        for($i=0;$i -lt $baseline.frames.Count;$i++){
            if((Get-FileHash -LiteralPath $baseline.frames[$i].FullName).Hash -cne (Get-FileHash -LiteralPath $enabled.frames[$i].FullName).Hash){$changed++}
        }
        if(!$changed){
            $taskReceipt.effects+=@{name=$effect;status='not_triggered_in_this_scene';video=$null}
            Save-DemoReceipt;Remove-OwnedFrames $enabled
            Write-Output "No visible difference for $effect in this scene; no misleading clip published"
            continue
        }
        $video=Join-Path $taskOutput ($slug+'.mp4')
        $note=if($taskNotes.ContainsKey($effect)){$taskNotes[$effect]}else{'One enhancement enabled; identical gameplay inputs on both sides'}
        $filter="[0:v]scale=640:480:flags=neighbor,pad=640:564:0:50:color=0x14202c,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='$($spec.left)':fontsize=22:fontcolor=white:x=20:y=14[left];"+
            "[1:v]scale=640:480:flags=neighbor,pad=640:564:0:50:color=0x14202c,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='$effect - HIGH':fontsize=22:fontcolor=white:x=20:y=14[right];"+
            "[left][right]hstack=inputs=2,setsar=1,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='$note':fontsize=20:fontcolor=white:x=(w-tw)/2:y=539[out]"
        & $taskFfmpeg -hide_banner -loglevel error -nostdin -f concat -safe 0 -i $baseline.concat `
            -f concat -safe 0 -i $enabled.concat -filter_complex $filter -map '[out]' `
            -frames:v $taskVideoFrames -r 30 -c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p -movflags +faststart $video
        if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $video)){throw 'Video encoding failed'}
        $taskReceipt.effects+=@{name=$effect;key=$spec.key;value=$spec.value;context=$spec.context;stage=$spec.stage;left=$spec.left;status='requires_visual_review';
            changed_frames=$changed;video=[IO.Path]::GetFileName($video);sha256=(Get-FileHash -LiteralPath $video).Hash}
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
