param(
    [string[]]$InputDirectories=@('build/validation/active/global-effect-demos-0.0.8.5','build/validation/active/scene-effect-demos-0.0.8.5'),
    [string]$Output='build/validation/active/StarFoxEnhanced-0.0.8.5-global-effects-demo.mp4'
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
        $duration=[long]($receipt.seconds*1000)
        $taskConcat+="file '"+$path.Replace('\','/').Replace("'","'\''")+"'"
        $title=$effect.name.Replace('\','\\').Replace('=','\=').Replace(';','\;').Replace('#','\#')
        $taskMetadata+=@('[CHAPTER]','TIMEBASE=1/1000',"START=$taskTime",('END='+($taskTime+$duration)),"title=$title")
        $taskEntries+=@{name=$effect.name;start_seconds=$taskTime/1000;duration_seconds=$receipt.seconds}
        $taskTime+=$duration;$taskBytes+=(Get-Item -LiteralPath $path).Length
    }
}
if(!$taskEntries.Count){throw 'No verified clips'}
[void](& (Join-Path $taskValidation 'check-storage.ps1') -PlannedBytes ($taskBytes+1048576))
$taskList=$taskOutput+'.ffconcat';$taskChapters=$taskOutput+'.ffmetadata'
$taskConcat|Out-File -LiteralPath $taskList -Encoding ascii
$taskMetadata|Out-File -LiteralPath $taskChapters -Encoding utf8NoBOM
# Correct pixel aspect in both H.264 and the MP4 container without reencoding
# the captured scene. This also normalizes earlier clips' scaler-generated SAR.
& ffmpeg -hide_banner -loglevel error -nostdin -f concat -safe 0 -i $taskList -i $taskChapters `
    -map 0:v -map_metadata 1 -map_chapters 1 -c:v copy -bsf:v 'h264_metadata=sample_aspect_ratio=1/1' `
    -aspect '1280:530' -movflags +faststart $taskOutput
if($LASTEXITCODE -ne 0){throw 'Demonstration assembly failed'}
$taskProbe=& ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate,sample_aspect_ratio,nb_frames -show_entries format=duration -of json $taskOutput
if($LASTEXITCODE -ne 0){throw 'Assembled video verification failed'}
$taskInfo=$taskProbe|ConvertFrom-Json
if($taskInfo.streams[0].width -ne 1280 -or $taskInfo.streams[0].height -ne 530 -or $taskInfo.streams[0].sample_aspect_ratio -ne '1:1' -or [math]::Abs([double]$taskInfo.format.duration-$taskTime/1000) -gt 0.1){throw 'Video dimensions, aspect or duration differ'}
@{video=[IO.Path]::GetFileName($taskOutput);sha256=(Get-FileHash -LiteralPath $taskOutput).Hash;
    seconds=$taskTime/1000;clips=$taskEntries;verified=$true}|ConvertTo-Json -Depth 5|
    Out-File -LiteralPath ($taskOutput+'.json') -Encoding utf8NoBOM
Write-Output "$($taskEntries.Count) separate effect comparisons assembled: $taskOutput"
