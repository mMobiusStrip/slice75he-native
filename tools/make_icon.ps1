# Generates src/app.ico: an orange-accented keycap on a dark rounded square (16/32/48/256 px, PNG entries).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-RoundedPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function New-IconPng([int]$s) {
    $bmp = New-Object Drawing.Bitmap $s, $s
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([Drawing.Color]::Transparent)

    $bg = New-RoundedPath 0 0 $s $s ($s * 0.22)
    $g.FillPath((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 24, 24, 28))), $bg)

    # keycap: skirt + top
    $m = $s * 0.16
    $cap = New-RoundedPath $m ($m * 1.05) ($s - 2 * $m) ($s - 2 * $m) ($s * 0.12)
    $g.FillPath((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 200, 104, 34))), $cap)
    $t = $s * 0.24
    $top = New-RoundedPath $t ($t * 0.95) ($s - 2 * $t) ($s - 2 * $t - $s * 0.05) ($s * 0.09)
    $g.FillPath((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 242, 146, 64))), $top)

    if ($s -ge 32) {
        $font = New-Object Drawing.Font 'Segoe UI', ($s * 0.24), ([Drawing.FontStyle]::Bold), ([Drawing.GraphicsUnit]::Pixel)
        $fmt = New-Object Drawing.StringFormat
        $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
        $rect = New-Object Drawing.RectangleF $t, ($t * 0.95), ($s - 2 * $t), ($s - 2 * $t - $s * 0.05)
        $g.DrawString('HE', $font, (New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 40, 22, 8))), $rect, $fmt)
    }
    $g.Dispose()
    $ms = New-Object IO.MemoryStream
    $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return , $ms.ToArray()
}

$sizes = 16, 32, 48, 256
$images = foreach ($s in $sizes) { , (New-IconPng $s) }
$out = New-Object IO.MemoryStream
$w = New-Object IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $images[$i].Length
    $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256)); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$len); $w.Write([uint32]$offset)
    $offset += $len
}
foreach ($img in $images) { $w.Write($img) }
$w.Flush()
$dest = Join-Path $PSScriptRoot '..\src\app.ico'
[IO.File]::WriteAllBytes($dest, $out.ToArray())
"Wrote $dest ($($out.Length) bytes)"
