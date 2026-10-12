param(
    [string[]]$InputDirectories=@('build/validation/active/global-effect-demos-0.0.8.5','build/validation/active/scene-effect-demos-0.0.8.5'),
    [string]$Output='build/validation/active/StarFoxEnhanced-0.0.8.5-global-effects-demo.mp4',
    [string[]]$Effects=@(),
    [hashtable]$Details=@{},
    [ValidateRange(1,5)][int]$DetailSeconds=2
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskValidation=Join-Path $taskRoot 'build/validation'
$taskOutput=[IO.Path]::GetFullPath((Join-Path $taskRoot $Output))
if(!$taskOutput.StartsWith($taskValidation+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw 'Output must remain under build/validation'
}
if(Test-Path -LiteralPath $taskOutput){throw 'Preserve the existing demonstration video'}
$taskConcat=@('ffconcat version 1.0')
$taskMetadata=@(';FFMETADATA1','title=Star Fox Enhanced - Global Enhancements','comment=One effect at a time; matching gameplay; OFF left, HIGH right. Shadow Softness compares HARD versus HIGH.')
$taskEntries=@();$taskTime=0L;$taskBytes=0L
$taskNames=@();$taskClips=@{};$taskWidth=0;$taskHeight=0
foreach($input in $InputDirectories){
    $directory=[IO.Path]::GetFullPath((Join-Path $taskRoot $input))
    if(!$directory.StartsWith($taskValidation+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe demo input directory'}
    $receipt=Get-Content -LiteralPath (Join-Path $directory 'demos.json') -Raw|ConvertFrom-Json
    if(!$receipt.complete){throw "Captures are not complete: $input"}
    foreach($effect in $receipt.effects){
        if(!$effect.video){continue}
        $path=[IO.Path]::GetFullPath((Join-Path $directory $effect.video))
        if(!$path.StartsWith($directory+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($path) -ne '.mp4'){throw 'Unsafe clip path'}
        if((Get-FileHash -LiteralPath $path).Hash -cne $effect.sha256){throw 'Recorded clip changed after capture'}
        if(!$taskClips.ContainsKey($effect.name)){$taskNames+=$effect.name}
        # Later directories supersede a recaptured effect, without showing it twice.
        $taskClips[$effect.name]=@{path=$path;effect=$effect;receipt=$receipt}
    }
}
if($Effects.Count){
    if(@($Effects|Select-Object -Unique).Count -ne $Effects.Count){throw 'Duplicate requested effect'}
    foreach($name in $Effects){if(!$taskClips.ContainsKey($name)){throw "Missing effect: $name"}}
    $taskNames=$Effects
}
if(!$taskNames.Count){throw 'No verified clips'}
foreach($name in $taskNames){$taskBytes+=(Get-Item -LiteralPath $taskClips[$name].path).Length}
[void](& (Join-Path $taskValidation 'check-storage.ps1') -PlannedBytes ($taskBytes*3+8388608))
$taskParts=$taskOutput+'.parts';$taskOwnedParts=@()
if($Details.Count){
    if(Test-Path -LiteralPath $taskParts){throw 'Preserve existing demonstration scratch'}
    New-Item -ItemType Directory -Path $taskParts|Out-Null
}
foreach($name in $taskNames){
    $clip=$taskClips[$name];$path=$clip.path;$receipt=$clip.receipt;$effect=$clip.effect
    $probe=& ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate,nb_frames -show_entries format=duration -of json $path
    if($LASTEXITCODE -ne 0){throw 'Input clip probe failed'}
    $info=$probe|ConvertFrom-Json;$stream=$info.streams[0]
    if(!$taskWidth){$taskWidth=[int]$stream.width;$taskHeight=[int]$stream.height}
    if($stream.width -ne $taskWidth -or $stream.height -ne $taskHeight -or $stream.r_frame_rate -ne '30/1'){
        throw 'Comparison clips must have matching dimensions and 30-FPS timing'
    }
    $seconds=[double]$info.format.duration;$detail=$null
    if($Details.ContainsKey($name)){
        if($taskWidth -ne 1280 -or $taskHeight -ne 564){throw 'Paused details require the 1280x564 comparison layout'}
        $detail=$Details[$name];$frame=[int][math]::Round([double]$detail.time*30)
        if($frame -lt 0 -or $frame -ge [int]$stream.nb_frames){throw "Detail frame outside clip: $name"}
        $zoom=if($detail.ContainsKey('zoom')){[double]$detail.zoom}else{1.}
        if($zoom -lt 1 -or $zoom -gt 3){throw 'Detail zoom must be between 1x and 3x'}
        $cw=[int](2*[math]::Floor(640/$zoom/2));$ch=[int](2*[math]::Floor(480/$zoom/2))
        $cx=if($detail.ContainsKey('x')){[double]$detail.x}else{320.}
        $cy=if($detail.ContainsKey('y')){[double]$detail.y}else{240.}
        $x=[int][math]::Clamp([math]::Round($cx-$cw/2),0,640-$cw)
        $y=[int][math]::Clamp([math]::Round($cy-$ch/2),0,480-$ch)
        $label=$name.Replace("'",'')
        $left=if($effect.PSObject.Properties.Name -contains 'left'){$effect.left}else{'OFF'}
        $text="PAUSED DETAIL - ${zoom}x crop of the same gameplay frame"
        $filter="[0:v]split=3[play][sl][sr];"+
            "[sl]trim=start_frame=${frame}:end_frame=$($frame+1),setpts=PTS-STARTPTS,crop=640:480:0:50,crop=${cw}:${ch}:${x}:${y},scale=640:480:flags=neighbor,pad=640:564:0:50:color=0x14202c,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='$left':fontsize=22:fontcolor=white:x=20:y=14[dl];"+
            "[sr]trim=start_frame=${frame}:end_frame=$($frame+1),setpts=PTS-STARTPTS,crop=640:480:640:50,crop=${cw}:${ch}:${x}:${y},scale=640:480:flags=neighbor,pad=640:564:0:50:color=0x14202c,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='$label - HIGH':fontsize=22:fontcolor=white:x=20:y=14[dr];"+
            "[dl][dr]hstack,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='$text':fontsize=20:fontcolor=white:x=(w-tw)/2:y=539,tpad=stop_mode=clone:stop_duration=$DetailSeconds,trim=duration=$DetailSeconds,setpts=N/(30*TB)[held];"+
            '[play][held]concat=n=2:v=1:a=0,setsar=1[out]'
        $part=Join-Path $taskParts ($name.ToLowerInvariant().Replace(' ','-')+'.mp4')
        & ffmpeg -hide_banner -loglevel error -nostdin -i $path -filter_complex $filter -map '[out]' -r 30 -c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p -movflags +faststart $part
        if($LASTEXITCODE -ne 0){throw "Paused detail encoding failed: $name"}
        $taskOwnedParts+=$part;$path=$part;$seconds+=$DetailSeconds
    }
    $duration=[long][math]::Round($seconds*1000)
    $taskConcat+="file '"+$path.Replace('\','/').Replace("'","'\''")+"'"
    $title=$name.Replace('\','\\').Replace('=','\=').Replace(';','\;').Replace('#','\#')
    $taskMetadata+=@('[CHAPTER]','TIMEBASE=1/1000',"START=$taskTime",('END='+($taskTime+$duration)),"title=$title")
    $taskEntries+=@{name=$name;start_seconds=$taskTime/1000;duration_seconds=$seconds;
        source_sha256=$effect.sha256;binary_sha256=$receipt.binary_sha256;stage=$effect.stage;detail=$detail}
    $taskTime+=$duration
}
$taskList=$taskOutput+'.ffconcat';$taskChapters=$taskOutput+'.ffmetadata'
$taskConcat|Out-File -LiteralPath $taskList -Encoding ascii
$taskMetadata|Out-File -LiteralPath $taskChapters -Encoding utf8NoBOM
# Correct pixel aspect in both H.264 and the MP4 container without reencoding
# the captured scene. This also normalizes earlier clips' scaler-generated SAR.
& ffmpeg -hide_banner -loglevel error -nostdin -f concat -safe 0 -i $taskList -i $taskChapters `
    -map 0:v -map_metadata 1 -map_chapters 1 -c:v copy -bsf:v 'h264_metadata=sample_aspect_ratio=1/1' `
    -movflags +faststart $taskOutput
if($LASTEXITCODE -ne 0){throw 'Demonstration assembly failed'}
$taskProbe=& ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate,sample_aspect_ratio,nb_frames -show_entries format=duration -of json $taskOutput
if($LASTEXITCODE -ne 0){throw 'Assembled video verification failed'}
$taskInfo=$taskProbe|ConvertFrom-Json
if($taskInfo.streams[0].width -ne $taskWidth -or $taskInfo.streams[0].height -ne $taskHeight -or $taskInfo.streams[0].sample_aspect_ratio -ne '1:1' -or [math]::Abs([double]$taskInfo.format.duration-$taskTime/1000) -gt 0.1){throw 'Video dimensions, aspect or duration differ'}
@{video=[IO.Path]::GetFileName($taskOutput);sha256=(Get-FileHash -LiteralPath $taskOutput).Hash;
    seconds=$taskTime/1000;clips=$taskEntries;verified=$true}|ConvertTo-Json -Depth 5|
    Out-File -LiteralPath ($taskOutput+'.json') -Encoding utf8NoBOM
foreach($part in $taskOwnedParts){
    $safe=[IO.Path]::GetFullPath($part)
    if(!$safe.StartsWith($taskParts+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe intermediate cleanup path'}
    Remove-Item -LiteralPath $safe
}
if($Details.Count -and !(Get-ChildItem -LiteralPath $taskParts -Force)){Remove-Item -LiteralPath $taskParts}
Write-Output "$($taskEntries.Count) separate effect comparisons assembled: $taskOutput"
