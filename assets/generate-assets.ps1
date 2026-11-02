<#
.SYNOPSIS
  Renders the OpenMagic icon and installer wizard images.

.DESCRIPTION
  Writes assets\openmagic.ico (16-256 px), assets\openmagic.png (512 px) and the
  Inno Setup wizard bitmaps in installer\ (100% and 200% scale). Run with
  Windows PowerShell 5.1 (System.Drawing).
#>
Add-Type -AssemblyName System.Drawing

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$indigoLight = [System.Drawing.Color]::FromArgb(99, 102, 241)
$indigoDark = [System.Drawing.Color]::FromArgb(49, 46, 129)

function New-RoundedPath([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = 2 * $r
  $path.AddArc($x, $y, $d, $d, 180, 90)
  $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
  $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
  $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
  $path.CloseFigure()
  return $path
}

# The mark: a Magic Mouse outline with a scroll stroke, drawn in a square area.
function Draw-Mark([System.Drawing.Graphics]$g, [single]$x, [single]$y, [single]$size, [System.Drawing.Color]$color) {
  $penWidth = [Math]::Max(1.0, $size * 0.075)
  $w = $size * 0.50; $h = $size * 0.78
  $mx = $x + ($size - $w) / 2; $my = $y + ($size - $h) / 2
  $pen = New-Object System.Drawing.Pen($color, $penWidth)
  $outline = New-RoundedPath $mx $my $w $h ($w / 2)
  $g.DrawPath($pen, $outline)
  $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
  $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $cx = $x + $size / 2
  $g.DrawLine($pen, $cx, $my + $h * 0.20, $cx, $my + $h * 0.40)
  $pen.Dispose(); $outline.Dispose()
}

function New-IconBitmap([int]$size) {
  $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)
  $background = New-RoundedPath 0 0 ($size - 0.5) ($size - 0.5) ($size * 0.22)
  $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.PointF(0, 0)), (New-Object System.Drawing.PointF($size, $size)), $indigoLight, $indigoDark)
  $g.FillPath($brush, $background)
  Draw-Mark $g ($size * 0.18) ($size * 0.12) ($size * 0.64) ([System.Drawing.Color]::White)
  $brush.Dispose(); $background.Dispose(); $g.Dispose()
  return $bmp
}

function Get-PngBytes([System.Drawing.Bitmap]$bmp) {
  $stream = New-Object System.IO.MemoryStream
  $bmp.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
  return , $stream.ToArray()
}

# ICO with PNG-compressed images (supported by Windows Vista and later).
$sizes = 16, 24, 32, 48, 64, 128, 256
$images = foreach ($s in $sizes) { $b = New-IconBitmap $s; , (Get-PngBytes $b); $b.Dispose() }
$ico = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($ico)
$writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
  $writer.Write([Byte]$dim); $writer.Write([Byte]$dim); $writer.Write([Byte]0); $writer.Write([Byte]0)
  $writer.Write([UInt16]1); $writer.Write([UInt16]32)
  $writer.Write([UInt32]$images[$i].Length); $writer.Write([UInt32]$offset)
  $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write($image) }
[System.IO.File]::WriteAllBytes((Join-Path $root 'assets\openmagic.ico'), $ico.ToArray())

$large = New-IconBitmap 512
$large.Save((Join-Path $root 'assets\openmagic.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$large.Dispose()

# Wizard side image (164x314 at 100%): gradient, mark and product name.
function Save-WizardImage([int]$scale, [string]$file) {
  $w = 164 * $scale; $h = 314 * $scale
  $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.PointF(0, 0)), (New-Object System.Drawing.PointF($w, $h)), $indigoLight, $indigoDark)
  $g.FillRectangle($brush, 0, 0, $w, $h)
  Draw-Mark $g ($w * 0.25) ($h * 0.16) ($w * 0.50) ([System.Drawing.Color]::White)

  $center = New-Object System.Drawing.StringFormat
  $center.Alignment = [System.Drawing.StringAlignment]::Center
  $title = New-Object System.Drawing.Font('Segoe UI Semibold', (15 * $scale), [System.Drawing.GraphicsUnit]::Pixel)
  $subtitle = New-Object System.Drawing.Font('Segoe UI', (10 * $scale), [System.Drawing.GraphicsUnit]::Pixel)
  $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
  $soft = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(199, 210, 254))
  $g.DrawString('OpenMagic', $title, $white, (New-Object System.Drawing.RectangleF(0, ($h * 0.52), $w, (30 * $scale))), $center)
  $g.DrawString("Magic Keyboard`nMagic Mouse 2", $subtitle, $soft, (New-Object System.Drawing.RectangleF(0, ($h * 0.52 + 26 * $scale), $w, (40 * $scale))), $center)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Bmp)
  $g.Dispose(); $bmp.Dispose(); $brush.Dispose()
}

# Wizard header image (55x55 at 100%): the icon on the wizard's white header.
function Save-WizardSmallImage([int]$scale, [string]$file) {
  $s = 55 * $scale
  $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::White)
  $icon = New-IconBitmap ([int]($s * 0.9))
  $g.DrawImage($icon, [int]($s * 0.05), [int]($s * 0.05))
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Bmp)
  $icon.Dispose(); $g.Dispose(); $bmp.Dispose()
}

foreach ($scale in 1, 2) {
  Save-WizardImage $scale (Join-Path $root "installer\wizard-$($scale * 100).bmp")
  Save-WizardSmallImage $scale (Join-Path $root "installer\wizard-small-$($scale * 100).bmp")
}
'Assets written.'
