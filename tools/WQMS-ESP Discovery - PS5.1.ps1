$HttpTimeout = 2
$Throttle = 50

Write-Host ""
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "   WQMS Parallel Discovery (PS 5.1)"
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# ===== RESULT STORAGE =====
$results = @()

# ===== RUNSPACE POOL =====
$runspacePool = [runspacefactory]::CreateRunspacePool(1, $Throttle)
$runspacePool.Open()

$tasks = @()

$interfaces = Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object {
        $_.IPAddress -notlike "169.254*" -and
		$_.IPAddress -ne (((ipconfig /all) -join "`n") -split "`n`n" | Where-Object {$_ -match "VirtualBox"} | ForEach-Object {$_ -match "IPv4 Address[ .]+: (\d+\.\d+\.\d+\.\d+)" > $null; $Matches[1]}) -and
        $_.IPAddress -ne "127.0.0.1"
    }

# ===== BUILD TASK LIST FIRST =====
$ipList = @()

foreach ($iface in $interfaces)
{
    $octets = $iface.IPAddress.Split('.')
    $subnet = "$($octets[0]).$($octets[1]).$($octets[2])"

    for ($i = 1; $i -le 254; $i++)
    {
        $ipList += "$subnet.$i"
    }
}

$total = $ipList.Count
$completed = 0

Write-Host "Total hosts to scan: $total"
Write-Host ""

# ===== RUN PARALLEL TASKS =====
foreach ($ip in $ipList)
{
    $ps = [powershell]::Create()
    $ps.RunspacePool = $runspacePool

    $scriptBlock = {
        param($ip, $HttpTimeout)

        try
        {
            if (-not (Test-Connection -ComputerName $ip -Count 1 -Quiet -ErrorAction SilentlyContinue))
            {
                return $null
            }

            $echo = Invoke-RestMethod "http://$ip/api/echo" -TimeoutSec $HttpTimeout -ErrorAction Stop

            if ($echo.response -ne "WQMS" -or $echo.status -ne "ok")
            {
                return $null
            }

            $status = Invoke-RestMethod "http://$ip/api/status" -TimeoutSec $HttpTimeout -ErrorAction Stop

            return [PSCustomObject]@{
                SystemName = $status.system_name
                IP         = $ip
                Version    = $status.version
                MAC        = $status.mac_address
                APSSID     = $status.ap_ssid
                Uptime     = $status.uptime
                Heap       = $status.free_heap
            }
        }
        catch
        {
            return $null
        }
    }

    $ps.AddScript($scriptBlock).AddArgument($ip).AddArgument($HttpTimeout) | Out-Null

    $handle = $ps.BeginInvoke()

    $tasks += [PSCustomObject]@{
        PS     = $ps
        Handle = $handle
    }
}

# ===== COLLECT RESULTS + PROGRESS =====
foreach ($task in $tasks)
{
    $result = $task.PS.EndInvoke($task.Handle)

    if ($result)
    {
        $results += $result
    }

    $task.PS.Dispose()

    # ===== PROGRESS UPDATE =====
    $completed++
    $percent = [math]::Round(($completed / $total) * 100, 1)

    $barLength = 30
    $filled = [math]::Round(($percent / 100) * $barLength)
    $bar = ("#" * $filled).PadRight($barLength, '-')

    Write-Host -NoNewline "`rScanning [$bar] $percent% ($completed/$total)"
}

Write-Host ""
Write-Host ""
Write-Host "Scan completed." -ForegroundColor Green

# ===== CLEAN RESULTS =====
$deviceList =
    $results |
    Sort-Object MAC -Unique |
    Sort-Object SystemName

if (-not $deviceList)
{
    Write-Host "No devices found." -ForegroundColor Yellow
    sleep 20
    return
}

if (!$deviceList.psobject.Properties['Count'] -or ($deviceList.Count -eq 1))
{
    Start-Process "http://$($deviceList[0].IP)"
    return
}
try
{
    $selected =
        $deviceList |
        Select-Object SystemName, IP, Version, MAC |
        Out-GridView -Title "Select WQMS Device" -PassThru

    if ($selected)
    {
        Start-Process "http://$($selected.IP)"
    }
}
catch
{
    for ($i = 0; $i -lt $deviceList.Count; $i++)
    {
        Write-Host "$($i+1). $($deviceList[$i].SystemName) ($($deviceList[$i].IP))"
    }

    $i = Read-Host "Select device number"
    Start-Process "http://$($deviceList[$i-1].IP)"
}