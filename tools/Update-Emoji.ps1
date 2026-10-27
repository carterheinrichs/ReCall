$ErrorActionPreference = 'Stop'
$text = (Invoke-WebRequest 'https://www.unicode.org/Public/emoji/16.0/emoji-test.txt').Content
if ($text -is [byte[]]) { $text = [Text.Encoding]::UTF8.GetString($text) }
$group = ''; $subgroup = ''
$items = foreach ($line in ($text -split "`n")) {
    if ($line.StartsWith('# group:')) { $group = $line.Substring(8).Trim() }
    elseif ($line.StartsWith('# subgroup:')) { $subgroup = $line.Substring(11).Trim() }
    elseif ($line -match '^([0-9A-F ]+)\s*; fully-qualified\s*#\s*\S+\s+E[0-9.]+\s+(.+)$') {
        $codes = $Matches[1].Trim(); $name = $Matches[2].Trim()
        $glyph = -join ($codes -split '\s+' | ForEach-Object { [char]::ConvertFromUtf32([Convert]::ToInt32($_, 16)) })
        @{ glyph = $glyph; shortcode = $name.Replace(' ', '_'); tags = "$name $group $subgroup" }
    }
}
$target = Join-Path $PSScriptRoot '../assets/emoji.json'
[IO.File]::WriteAllText($target, (ConvertTo-Json -InputObject @($items) -Depth 5 -Compress), [Text.UTF8Encoding]::new($false))
"Generated $($items.Count) Unicode 16 emoji."
