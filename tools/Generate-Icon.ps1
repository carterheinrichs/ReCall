# Regenerate the checked-in, multi-resolution native application/tray icon.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$images = @()
foreach ($size in $sizes) {
    $bitmap = [Drawing.Bitmap]::new($size, $size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $path = [Drawing.Drawing2D.GraphicsPath]::new()
    $font = [Drawing.Font]::new('Segoe UI', [single]($size * 0.66), [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $format = [Drawing.StringFormat]::new()
    $brush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(54, 65, 91))
    $png = [IO.MemoryStream]::new()
    try {
        $graphics.Clear([Drawing.Color]::Transparent)
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $inset = [single]($size / 32.0)
        $edge = [single]($size - 2 * $inset)
        $corner = [single]($size * 0.4)
        $path.AddArc($inset, $inset, $corner, $corner, 180, 90)
        $path.AddArc(($inset + $edge - $corner), $inset, $corner, $corner, 270, 90)
        $path.AddArc(($inset + $edge - $corner), ($inset + $edge - $corner), $corner, $corner, 0, 90)
        $path.AddArc($inset, ($inset + $edge - $corner), $corner, $corner, 90, 90)
        $path.CloseFigure()
        $graphics.FillPath($brush, $path)
        $format.Alignment = 'Center'; $format.LineAlignment = 'Center'
        $graphics.DrawString('R', $font, [Drawing.Brushes]::White, [Drawing.RectangleF]::new(0, 0, $size, $size), $format)
        $bitmap.Save($png, [Drawing.Imaging.ImageFormat]::Png)
        $images += ,$png.ToArray()
    } finally {
        $png.Dispose(); $brush.Dispose(); $format.Dispose(); $font.Dispose()
        $path.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
    }
}
$stream = [IO.MemoryStream]::new()
$writer = [IO.BinaryWriter]::new($stream)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
        $offset += $images[$i].Length
    }
    foreach ($image in $images) { $writer.Write([byte[]]$image) }
    $writer.Flush()
    $target = Join-Path $PSScriptRoot '../assets/ReCall.ico'
    [IO.File]::WriteAllBytes($target, $stream.ToArray())
    "Generated ReCall.ico with $($sizes.Count) icon sizes."
} finally { $writer.Dispose(); $stream.Dispose() }
