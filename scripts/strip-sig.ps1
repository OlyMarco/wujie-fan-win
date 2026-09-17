# Strip Authenticode signature (certificate table) from a PE file, section-aware.
param([string]$Path)
$bytes = [IO.File]::ReadAllBytes($Path)
$peOff = [BitConverter]::ToInt32($bytes, 0x3C)
$numSec = [BitConverter]::ToUInt16($bytes, $peOff + 6)
$optSize = [BitConverter]::ToUInt16($bytes, $peOff + 20)
$optOff = $peOff + 24
$magic = [BitConverter]::ToUInt16($bytes, $optOff)
if ($magic -eq 0x20B) { $ddOff = $optOff + 0x70 } else { $ddOff = $optOff + 0x60 }
$certRva = [BitConverter]::ToInt32($bytes, $ddOff + 4*8)
$certSize = [BitConverter]::ToInt32($bytes, $ddOff + 4*8 + 4)
if ($certRva -eq 0 -or $certSize -eq 0) { Write-Output "no signature"; exit 0 }
# end of image in FILE = max(PointerToRawData + SizeOfRawData) over sections
$secOff = $optOff + $optSize
$imgEnd = 0
for ($s = 0; $s -lt $numSec; $s++) {
    $base = $secOff + $s * 40
    $end = [BitConverter]::ToInt32($bytes, $base + 16) + [BitConverter]::ToInt32($bytes, $base + 8)
    if ($end -gt $imgEnd) { $imgEnd = $end }
}
if ($imgEnd -lt 1 -or $imgEnd -gt $bytes.Length) { Write-Output "bad layout"; exit 1 }
for ($i = 0; $i -lt 8; $i++) { $bytes[$ddOff + 4*8 + $i] = 0 }
$out = New-Object byte[] $imgEnd
[Array]::Copy($bytes, $out, $imgEnd)
[IO.File]::WriteAllBytes($Path, $out)
Write-Output ("stripped to {0} bytes (was {1}, cert {2} bytes)" -f $imgEnd, $bytes.Length, $certSize)
