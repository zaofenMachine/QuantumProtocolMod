#Requires -Version 7.2
Set-StrictMode -Version 3
$script:PartNames = @('main','spawn','hand','qualification','layout','charge','turn')
$script:FixedFiles = [ordered]@{
    main = 'route-c.json'
    spawn = 'route-c-exact-spawn-plan.json'
    hand = 'route-c-exact-player-hand-health.json'
    qualification = 'route-c-exact-player-qualification.json'
    zones = 'route-c-exact-player-zones.json'
    trash = 'route-c-exact-player-trash.json'
    field = 'route-c-exact-player-field.json'
    charge = 'route-c-exact-character-charge.json'
    turn = 'route-c-exact-turn-progress.json'
}
function Assert-StatusValue([bool]$Condition,[string]$Message) {
    if (!$Condition) {
        throw [FormatException]::new($Message)
    }
}
function Get-StatusPath([string]$Path) {
    if (![IO.Path]::IsPathFullyQualified($Path) -or $Path.StartsWith('\\') -or $Path.StartsWith('//')) {
        throw [ArgumentException]::new('ModRoot必须为本地绝对路径。')
    }
    $full = [IO.Path]::GetFullPath($Path)
    $cursor = $full
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $i = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
            if (($i.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or ($i.PSObject.Properties['LinkType'] -and
                ![string]::IsNullOrEmpty($i.LinkType))) {
                throw [IO.IOException]::new('不读取链接或重解析点。')
            }
        }
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if ($parent -eq $cursor) {
            break
        }
        $cursor = $parent
    }
    return $full
}
function Read-StatusBytes([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        return $null
    }
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw [IO.IOException]::new('状态文件不是普通文件。')
    }
    $f = [IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {
        if ($f.Length -gt 2097152) {
            throw [IO.IOException]::new('状态文件超过2MiB读取上限。')
        }
        $m = [IO.MemoryStream]::new()
        try {
            $buffer = [byte[]]::new(8192)
            while (($n = $f.Read($buffer,0,$buffer.Length)) -gt 0) {
                if ($m.Length+$n -gt 2097152) {
                    throw [IO.IOException]::new('读取期间文件超过上限。')
                }
                $m.Write($buffer,0,$n)
            }
            return ,$m.ToArray()
        }
        finally {
            $m.Dispose()
        }
    }
    finally {
        $f.Dispose()
    }
}
function Get-StatusHash([byte[]]$Bytes) {
    $h = [Security.Cryptography.SHA256]::Create()
    try {
        return [Convert]::ToHexString($h.ComputeHash($Bytes))
    }
    finally {
        $h.Dispose()
    }
}
function Convert-StatusNode($Node) {
    switch ($Node.ValueKind.ToString()) {
        'Object' {
            $map = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
            $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            foreach ($p in $Node.EnumerateObject()) {
                Assert-StatusValue ($seen.Add($p.Name)) 'JSON含重复或大小写冲突的属性。'
                $map.Add($p.Name,(Convert-StatusNode $p.Value))
            }
            return ,$map
        }
        'Array' {
            $items = [Collections.Generic.List[object]]::new()
            foreach ($v in $Node.EnumerateArray()) {
                $items.Add((Convert-StatusNode $v))
            }
            return ,$items.ToArray()
        }
        'String' {
            return $Node.GetString()
        }
        'Number' {
            [long]$integer = 0
            if ($Node.TryGetInt64([ref]$integer)) {
                return $integer
            }
            return $Node.GetDouble()
        }
        'True' {
            return $true
        }
        'False' {
            return $false
        }
        'Null' {
            return $null
        }
        default {
            throw [FormatException]::new('JSON值类型不可识别。')
        }
    }
}
function Read-StatusJson([byte[]]$Bytes) {
    $text = [Text.UTF8Encoding]::new($false,$true).GetString($Bytes)
    if ($text.Length -gt 0 -and $text[0] -eq [char]0xfeff) {
        $text = $text.Substring(1)
    }
    $options = [Text.Json.JsonDocumentOptions]::new()
    $options.MaxDepth = 32
    $options.AllowTrailingCommas = $false
    $options.CommentHandling = [Text.Json.JsonCommentHandling]::Disallow
    $doc = [Text.Json.JsonDocument]::Parse($text,$options)
    try {
        return Convert-StatusNode $doc.RootElement
    }
    finally {
        $doc.Dispose()
    }
}
function Test-StatusKeys($Map,[string[]]$Keys) {
    Assert-StatusValue ($Map -is [Collections.Generic.Dictionary[string,object]]) 'JSON对象类型错误。'
    Assert-StatusValue ($Map.Count -eq $Keys.Count) 'JSON字段集合不符。'
    foreach ($k in $Keys) {
        Assert-StatusValue ($Map.ContainsKey($k)) "缺少字段：$k"
    }
}
function Assert-StatusFailure($Value) {
    if ($null -eq $Value) {
        return
    }
    Test-StatusKeys $Value @('stage','category','reason')
    foreach ($k in @('stage','category','reason')) {
        Assert-StatusValue ($Value[$k] -is [string]) '拒绝原因字段类型错误。'
    }
    Assert-StatusValue (![string]::IsNullOrWhiteSpace($Value['stage'])) '拒绝原因缺少阶段。'
}
function Assert-StatusTime([string]$Text) {
    Assert-StatusValue ($Text -cmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{1,7})?Z$') '时间不是UTC格式。'
    [DateTimeOffset]$value = [DateTimeOffset]::MinValue
    Assert-StatusValue ([DateTimeOffset]::TryParse($Text,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::None,[ref]$value)) '时间不可解析。'
    return $value
}
function Read-StatusReceipt([byte[]]$Bytes) {
    $r = Read-StatusJson $Bytes
    Assert-StatusValue ($r -is [Collections.Generic.Dictionary[string,object]]) '回执不是JSON对象。'
    Assert-StatusValue ($r.ContainsKey('schemaVersion') -and $r['schemaVersion'] -is [long]) '回执版本不是整数。'
    if ($r['schemaVersion'] -ne 1) {
        throw [NotSupportedException]::new('未知回执版本。')
    }
    $keys = 'schemaVersion kind attemptId origin attemptedAtUtc finishedAtUtc capturedAtUtc mainPayloadChecksum gameExecutableSha256 sourceLevelName waveIndex automaticDeadlineTicks layoutKind layoutSchema qualificationPolicy qualificationLayoutChecksum qualificationLayoutKind qualificationMainChecksum handRequiredChecksum outcome playerBundle allSelectedComponentsCommitted diagnosticComplete fullPlayerExactRestorationProven invalidationStarted invalidationCompleted firstFailure firstPlayerRefusal components events'.Split(' ')
    Test-StatusKeys $r $keys
    $ints = @('schemaVersion','waveIndex','automaticDeadlineTicks','layoutSchema')
    $bools = @('allSelectedComponentsCommitted','diagnosticComplete','fullPlayerExactRestorationProven','invalidationStarted','invalidationCompleted')
    $special = @('firstFailure','firstPlayerRefusal','components','events')
    foreach ($k in $keys) {
        if ($ints -ccontains $k) {
            Assert-StatusValue ($r[$k] -is [long]) "整数类型错误：$k"
        }
        elseif($bools -ccontains $k) {
            Assert-StatusValue ($r[$k] -is [bool]) "布尔类型错误：$k"
        }
        elseif($special -cnotcontains $k) {
            Assert-StatusValue ($r[$k] -is [string]) "文本类型错误：$k"
        }
    }
    Assert-StatusValue ($r['kind'] -ceq 'route-c-capture-outcome' -and !$r['fullPlayerExactRestorationProven']) '回执类型或范围错误。'
    Assert-StatusValue ($r['origin'] -cin @('manual','automatic') -and $r['attemptId'] -cmatch '^[1-9][0-9]*:(manual|automatic):[1-9][0-9]*$') '尝试身份错误。'
    Assert-StatusValue ($r['attemptId'].Split(':')[1] -ceq $r['origin']) '尝试身份与来源矛盾。'
    $start = Assert-StatusTime $r['attemptedAtUtc']
    $finish = Assert-StatusTime $r['finishedAtUtc']
    Assert-StatusValue ($finish -ge $start -and $finish -le [DateTimeOffset]::UtcNow.AddMinutes(5)) '尝试时间顺序不符或位于未来。'
    if ($r['capturedAtUtc']) {
        $capture = Assert-StatusTime $r['capturedAtUtc']
        Assert-StatusValue ($capture -ge $start -and $capture -le $finish) '捕获时间不在尝试范围内。'
    }
    Assert-StatusFailure $r['firstFailure']
    Assert-StatusFailure $r['firstPlayerRefusal']
    Assert-StatusValue ($r['events'] -is [array] -and $r['events'].Count -le 128) '事件数组错误。'
    foreach ($e in $r['events']) {
        Assert-StatusValue ($null -ne $e) '事件为空。'
        Assert-StatusFailure $e
    }
    Assert-StatusValue ($r['components'] -is [array] -and $r['components'].Count -eq 7) '部件数组错误。'
    $cs = @{}
    $all = $r['diagnosticComplete']
    $player = $false
    for ($i = 0;$i -lt 7;$i++) {
        $c = $r['components'][$i]
        Test-StatusKeys $c @('name','selected','requiredForPlayer','status','path','payloadChecksum','sha256','hashStatus','error')
        foreach ($k in @('name','status','path','payloadChecksum','sha256','hashStatus','error')) {
            Assert-StatusValue ($c[$k] -is [string]) '部件文本类型错误。'
        }
        foreach ($k in @('selected','requiredForPlayer')) {
            Assert-StatusValue ($c[$k] -is [bool]) '部件布尔类型错误。'
        }
        Assert-StatusValue ($c['name'] -ceq $script:PartNames[$i]) '部件次序或名称错误。'
        Assert-StatusValue ($c['status'] -cin @('not-selected','prepared','writing','committed','failed','withdrawn') -and
            $c['hashStatus'] -cin @('not-attempted','observed-after-write','unavailable')) '部件状态未知。'
        Assert-StatusValue (($c['selected'] -and $c['status'] -cne 'not-selected') -or (!$c['selected'] -and $c['status'] -ceq 'not-selected')) '部件选择与状态矛盾。'
        if (!$c['selected']) {
            Assert-StatusValue ($c['path'] -ceq '' -and $c['payloadChecksum'] -ceq '' -and $c['sha256'] -ceq '' -and $c['error'] -ceq '' -and
                $c['hashStatus'] -ceq 'not-attempted') '未选中部件含遗留发布信息。'
        }
        $required = if ($c['name'] -cin @('qualification','layout')) {
            $c['selected']
        }
        elseif($c['name'] -ceq 'hand') {
            $r['components'][4]['selected'] -and $r['handRequiredChecksum'].Length -gt 0
        }
        else {
            $false
        }
        Assert-StatusValue ($c['requiredForPlayer'] -eq $required) '部件必需依赖标记与绑定不符。'
        Assert-StatusValue ($c['status'] -cne 'writing' -or $i -eq 0) '非主档出现writing状态。'
        if ($c['selected']) {
            Assert-StatusValue ($c['payloadChecksum'] -cmatch '^[0-9A-F]{16}$' -and $c['path'].Length -gt 0) '选中部件缺少路径或校验标识。'
            if ($c['status'] -cne 'committed') {
                $all = $false
            }
        }
        if ($c['hashStatus'] -ceq 'observed-after-write') {
            Assert-StatusValue ($c['status'] -ceq 'committed' -and $c['sha256'] -cmatch '^[0-9A-F]{64}$') '已观察哈希字段错误。'
        }
        else {
            Assert-StatusValue ($c['sha256'] -ceq '') '未观察的哈希不应带SHA。'
        }
        $cs[$c['name']] = $c
    }
    $main = $cs.main['status'] -ceq 'committed'
    $all = $all -and $main
    if ($main) {
        Assert-StatusValue ($r['mainPayloadChecksum'] -cmatch '^[0-9A-F]{16}$' -and $r['mainPayloadChecksum'] -ceq $cs.main['payloadChecksum'] -and
            $r['gameExecutableSha256'] -cmatch '^[0-9A-F]{64}$' -and $r['sourceLevelName'].Length -gt 0 -and $r['waveIndex'] -ge 0 -and
            $r['capturedAtUtc'].Length -gt 0) '已发布主档缺少明确绑定信息。'
    }
    Assert-StatusValue (($r['origin'] -ceq 'manual' -and $r['automaticDeadlineTicks'] -eq 0) -or ($r['origin'] -ceq 'automatic' -and
        $r['automaticDeadlineTicks'] -gt 0)) '尝试期限标记不完整。'
    $selected = $cs.layout['selected']
    # Recompute only the historical receipt claim, never restore eligibility.
    $player = (
        $selected -and $r['diagnosticComplete'] -and $main -and
        $cs.layout['status'] -ceq 'committed' -and
        $cs.qualification['status'] -ceq 'committed'
    )
    $player = (
        $player -and $r['mainPayloadChecksum'].Length -gt 0 -and
        $r['qualificationMainChecksum'] -ceq $r['mainPayloadChecksum'] -and
        $r['qualificationLayoutKind'] -ceq $r['layoutKind'] -and
        $r['qualificationLayoutChecksum'] -ceq $cs.layout['payloadChecksum']
    )
    if ($r['handRequiredChecksum']) {
        $player = $player -and $r['handRequiredChecksum'] -ceq $cs.hand['payloadChecksum']
    }
    foreach ($c in $cs.Values) {
        if ($c['requiredForPlayer']) {
            $player = $player -and $c['selected'] -and $c['status'] -ceq 'committed'
        }
    }
    $bundle = if ($player) {
        'committed-layout-and-qualification'
    }
    elseif($selected) {
        'selected-incomplete'
    }
    else {
        'unavailable'
    }
    $outcome = if (!$main) {
        'rejected-before-main'
    }
    elseif(!$all -or ($selected -and !$player)) {
        'partial-publication'
    }
    elseif($player) {
        'player-bundle-committed'
    }
    else {
        'main-only'
    }
    Assert-StatusValue ($r['outcome'] -ceq $outcome -and $r['playerBundle'] -ceq $bundle -and $r['allSelectedComponentsCommitted'] -eq $all) '回执声明与部件状态矛盾。'
    Assert-StatusValue (!$r['invalidationCompleted'] -or $r['invalidationStarted']) '清理标记矛盾。'
    return $r
}
function Get-StatusSample([string]$Root,[scriptblock]$Reader,[int]$Pass) {
    $population = [Collections.Generic.List[string]]::new()
    $cp = Get-StatusPath (Join-Path $Root 'Checkpoint')
    if (Test-Path -LiteralPath $cp) {
        if (!(Test-Path -LiteralPath $cp -PathType Container)) {
            throw [IO.IOException]::new('Checkpoint不是目录。')
        }
        foreach ($i in Get-ChildItem -LiteralPath $cp -Force -ErrorAction Stop) {
            [void](Get-StatusPath $i.FullName)
            if ($i.PSIsContainer) {
                throw [IO.IOException]::new('Checkpoint含未识别子目录。')
            }
            $population.Add($i.Name)
        }
    }
    $paths = [ordered]@{
        receipt = (Join-Path $Root 'Reports/route-c-capture-latest.json')
    }
    foreach ($k in $script:FixedFiles.Keys) {
        $paths[$k] = Join-Path $cp $script:FixedFiles[$k]
    }
    $files = @{}
    $total = 0
    foreach ($k in $paths.Keys) {
        $p = Get-StatusPath $paths[$k]
        $raw = if ($Reader) {
            & $Reader $p $Pass
        }
        else {
            Read-StatusBytes $p
        }
        if ($null -eq $raw) {
            $files[$k] = @{
                path = $p
                bytes = $null
                sha256 = $null
            }
            continue
        }
        if ($raw -isnot [byte[]]) {
            throw [IO.IOException]::new('读取器没有返回byte[]。')
        }
        if ($raw.Length -gt 2097152) {
            throw [IO.IOException]::new('文件超过2MiB上限。')
        }
        $total += $raw.Length
        if ($total -gt 23068672) {
            throw [IO.IOException]::new('状态读取总量超限。')
        }
        $files[$k] = @{
            path = $p
            bytes = $raw
            sha256 = (Get-StatusHash $raw)
        }
    }
    return @{
        population = @($population.ToArray()|Sort-Object -CaseSensitive)
        files = $files
    }
}
function Test-StatusSamples($A,$B) {
    if (($A.population-join"`n") -cne ($B.population-join"`n")) {
        return $false
    }
    foreach ($k in $A.files.Keys) {
        if ($A.files[$k].sha256 -cne $B.files[$k].sha256) {
            return $false
        }
    }
    return $true
}
function Get-CheckpointStatusCore {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ModRoot,
        [Parameter(DontShow)][scriptblock]$Reader
    )
    $result = [ordered]@{
        schemaVersion = 1
        kind = 'checkpoint-status-view'
        status = 'unknown'
        exitCode = 2
        modRoot = $ModRoot
        observedAtUtc = [DateTimeOffset]::UtcNow.ToString('o')
        receiptStatus = 'missing'
        relation = 'unknown'
        readConsistency = 'error'
        lastAttempt = $null
        components = @()
        issues = @()
        restoreAdmissionProven = $false
        fullPlayerExactRestorationProven = $false
    }
    try {
        $root = Get-StatusPath $ModRoot
        $result.modRoot = $root
        if (!(Test-Path -LiteralPath $root -PathType Container)) {
            throw [IO.IOException]::new('ModRoot目录不存在。')
        }
        # Read twice without writes or locks. This is sampled consistency, not
        # an atomic snapshot; any observed change discards the combined claim.
        $a = Get-StatusSample $root $Reader 1
        $b = Get-StatusSample $root $Reader 2
        if (!(Test-StatusSamples $a $b)) {
            $result.readConsistency = 'unstable'
            $result.issues = @('读取期间文件发生变化，请稍后重试。')
            return [pscustomobject]$result
        }
        $result.readConsistency = 'two-pass-sampled'
        $unbound = [Collections.Generic.List[object]]::new()
        foreach ($key in $script:FixedFiles.Keys) {
            $f = $a.files[$key]
            if ($key -ceq 'main' -or $null -ne $f.bytes) {
                $unbound.Add([pscustomobject]@{
                    name = $key
                    path = $f.path
                    recordedStatus = 'not-selected'
                    currentStatus = $(if ($null -eq $f.bytes) {
                        'absent'
                    }
                    else {
                        'present-unbound'
                    })
                    recordedSha256 = ''
                    currentSha256 = $f.sha256
                })
            }
        }
        $result.components = $unbound.ToArray()
        if ($null -eq $a.files.receipt.bytes) {
            $result.issues = @('没有保存回执；已有主档或旧补充文件不能据此证明来源。')
            return [pscustomobject]$result
        }
        try {
            $r = Read-StatusReceipt $a.files.receipt.bytes
        }
        catch [NotSupportedException] {
            $result.receiptStatus = 'unsupported'
            $result.issues = @($_.Exception.Message)
            return [pscustomobject]$result
        }
        catch {
            $result.receiptStatus = 'invalid'
            $result.issues = @($_.Exception.Message)
            return [pscustomobject]$result
        }
        # A valid receipt remains historical data. Fixed local files are read
        # independently; no component path from the receipt is opened.
        $result.receiptStatus = 'valid'
        $result.lastAttempt = [ordered]@{}
        foreach ($k in @('attemptId','origin','attemptedAtUtc','finishedAtUtc','outcome','playerBundle','diagnosticComplete','firstFailure','firstPlayerRefusal')) {
            $result.lastAttempt[$k] = $r[$k]
        }
        $issues = [Collections.Generic.List[string]]::new()
        $components = [Collections.Generic.List[object]]::new()
        $allMatch = $true
        $mainMatch = $false
        $layoutKeys = @{
            'route-c-exact-player-zones' = 'zones'
            'route-c-exact-player-trash' = 'trash'
            'route-c-exact-player-field' = 'field'
        }
        foreach ($c in $r['components']) {
            $name = $c['name']
            $key = if ($name -ceq 'layout') {
                if ($layoutKeys.ContainsKey($r['layoutKind'])) {
                    $layoutKeys[$r['layoutKind']]
                }
                else {
                    $null
                }
            }
            else {
                $name
            }
            $f = if ($key) {
                $a.files[$key]
            }
            else {
                $null
            }
            $state = 'not-recorded'
            $currentHash = if ($f) {
                $f.sha256
            }
            else {
                $null
            }
            $path = if ($f) {
                $f.path
            }
            else {
                ''
            }
            if ($c['selected']) {
                if (!$f) {
                    $state = 'unknown-layout'
                    $allMatch = $false
                }
                else {
                    $pathMatches = $false
                    try {
                        $recorded = $c['path']
                        if ([IO.Path]::IsPathFullyQualified($recorded) -and !$recorded.StartsWith('\\') -and !$recorded.StartsWith('//')) {
                            $pathMatches = [IO.Path]::GetFullPath($recorded)-ieq$path
                        }
                    }
                    catch {
                    }
                    if (!$pathMatches) {
                        $issues.Add("$name：回执原路径与所选目录不一致；未读取回执路径。")
                        $allMatch = $false
                    }
                    if ($c['status'] -cne 'committed') {
                        $state = $c['status']
                        $allMatch = $false
                    }
                    elseif($null -eq $f.bytes) {
                        $state = 'missing'
                        $allMatch = $false
                    }
                    elseif($c['hashStatus'] -cne 'observed-after-write') {
                        $state = 'unbound'
                        $allMatch = $false
                    }
                    elseif($currentHash -cne $c['sha256']) {
                        $state = 'changed'
                        $allMatch = $false
                    }
                    else {
                        $state = if ($pathMatches) {
                            'matching'
                        }
                        else {
                            'path-mismatch'
                        }
                        if ($name -ceq 'main') {
                            $mainMatch = $true
                        }
                    }
                }
            }
            elseif($f -and $null -ne $f.bytes) {
                $state = 'present-unbound'
            }
            $components.Add([pscustomobject]@{
                name = $name
                path = $path
                recordedStatus = $c['status']
                currentStatus = $state
                recordedSha256 = $c['sha256']
                currentSha256 = $currentHash
            })
        }
        $chosenLayout = if ($layoutKeys.ContainsKey($r['layoutKind'])) {
            $layoutKeys[$r['layoutKind']]
        }
        else {
            $null
        }
        foreach ($extra in @('zones','trash','field')) {
            if ($extra -cne $chosenLayout -and $null -ne $a.files[$extra].bytes) {
                $components.Add([pscustomobject]@{
                    name = $extra
                    path = $a.files[$extra].path
                    recordedStatus = 'not-selected'
                    currentStatus = 'present-unbound'
                    recordedSha256 = ''
                    currentSha256 = $a.files[$extra].sha256
                })
                $issues.Add("$extra：存在未由该回执关联的布局文件。")
            }
        }
        $result.components = $components.ToArray()
        if ($r['outcome'] -ceq 'rejected-before-main') {
            $result.relation = 'no-publication'
            $issues.Add('本次尝试未发布主档；当前检查点来源未由本回执证明。')
        }
        elseif(!$mainMatch) {
            $main = $components[0]
            $result.relation = if ($main.currentStatus -cin @('changed','missing')) {
                'stale'
            }
            else {
                'unknown'
            }
            $issues.Add('当前主档与回执不对应，或缺少可核对的观察哈希。')
        }
        elseif(!$allMatch -or !$r['diagnosticComplete']) {
            $result.relation = 'incomplete'
            $issues.Add('主档字节对应，但已记录依赖或路径不一致／不明。')
        }
        else {
            try {
                # Compare known header linkage only. No FNV/state/admission
                # verifier is recreated by this user-facing viewer.
                $documents = @{}
                $headers = @{
                    main = @('route-c-substage-restart',@(2))
                    spawn = @('route-c-exact-spawn-plan',@(1))
                    hand = @('route-c-exact-player-hand-health',@(3))
                    qualification = @('route-c-exact-player-qualification',@(2))
                    charge = @('route-c-exact-character-charge',@(1,2,3))
                    turn = @('route-c-exact-turn-progress',@(2))
                }
                $layoutSchemas = @{
                    'route-c-exact-player-zones' = @(7)
                    'route-c-exact-player-trash' = @(8)
                    'route-c-exact-player-field' = @(11,12,13)
                }
                foreach ($c in $r['components']) {
                    if (!$c['selected']) {
                        continue
                    }
                    $key = if ($c['name'] -ceq 'layout') {
                        $layoutKeys[$r['layoutKind']]
                    }
                    else {
                        $c['name']
                    }
                    $doc = Read-StatusJson $a.files[$key].bytes
                    Assert-StatusValue ($doc -is [Collections.Generic.Dictionary[string,object]] -and $doc.ContainsKey('payloadChecksum') -and
                        $doc['payloadChecksum'] -is [string] -and $doc['payloadChecksum'] -cmatch '^[0-9A-F]{16}$' -and $doc['payloadChecksum'] -ceq $c['payloadChecksum']) '当前文件的负载标识与回执矛盾。'
                    foreach ($k in @('capturedAtUtc','gameExecutableSha256','sourceLevelName','waveIndex')) {
                        Assert-StatusValue ($doc.ContainsKey($k) -and $doc[$k].GetType() -eq $r[$k].GetType() -and $doc[$k] -ceq $r[$k]) "文件绑定字段不一致：$k"
                    }
                    Assert-StatusValue ($doc.ContainsKey('schemaVersion') -and $doc['schemaVersion'] -is [long] -and $doc.ContainsKey('kind') -and
                        $doc['kind'] -is [string]) '文件版本或类型缺失。'
                    if ($c['name'] -ceq 'layout') {
                        Assert-StatusValue ($layoutSchemas.ContainsKey($r['layoutKind']) -and $layoutSchemas[$r['layoutKind']] -contains $r['layoutSchema'] -and
                            $doc['schemaVersion'] -eq $r['layoutSchema'] -and $doc['kind'] -ceq $r['layoutKind']) '布局版本尚不可识别或绑定矛盾。'
                        Assert-StatusValue ($doc.ContainsKey('qualificationPolicy') -and $doc['qualificationPolicy'] -is [string] -and
                            $doc['qualificationPolicy'] -ceq $r['qualificationPolicy'] -and $doc.ContainsKey('playerHandHealthChecksum') -and
                            $doc['playerHandHealthChecksum'] -is [string] -and $doc['playerHandHealthChecksum'] -ceq $r['handRequiredChecksum']) '布局资格或HAND依赖标识矛盾。'
                    }
                    else {
                        $expected = $headers[$c['name']]
                        Assert-StatusValue ($doc['kind'] -ceq $expected[0] -and $expected[1] -contains $doc['schemaVersion']) '文件版本或类型尚不可识别。'
                    }
                    if ($c['name'] -cne 'main') {
                        Assert-StatusValue ($doc.ContainsKey('routeCPayloadChecksum') -and $doc['routeCPayloadChecksum'] -is [string] -and
                            $doc['routeCPayloadChecksum'] -ceq $r['mainPayloadChecksum']) '补充文件不属于该主档。'
                    }
                    if ($c['name'] -ceq 'qualification') {
                        foreach ($pair in @(@('layoutKind','layoutKind'),@('layoutPayloadChecksum','qualificationLayoutChecksum'),@('qualificationPolicy','qualificationPolicy'))) {
                            Assert-StatusValue ($doc.ContainsKey($pair[0]) -and $doc[$pair[0]] -is [string] -and $doc[$pair[0]] -ceq $r[$pair[1]]) '资格与布局绑定矛盾。'
                        }
                    }
                    $documents[$c['name']] = $doc
                }
                if ($r['handRequiredChecksum']) {
                    Assert-StatusValue ($documents.ContainsKey('hand') -and $documents['hand']['payloadChecksum'] -ceq $r['handRequiredChecksum']) 'HAND依赖缺失或不匹配。'
                }
                $result.relation = 'matching-files'
            }
            catch {
                $result.relation = 'incomplete'
                $issues.Add($_.Exception.Message)
            }
        }
        if (!$r['diagnosticComplete']) {
            $result.relation = 'unknown'
            $issues.Add('回执诊断不完整，不能确认全部文件对应或本次是否发布。')
        }
        $result.issues = $issues.ToArray()
        $result.status = if ($result.relation -ceq 'unknown') {
            'unknown'
        }
        else {
            'observed'
        }
        $result.exitCode = if ($result.status -ceq 'unknown') {
            2
        }
        else {
            0
        }
        return [pscustomobject]$result
    }
    catch {
        $result.status = 'unreadable'
        $result.exitCode = 3
        $result.readConsistency = 'error'
        $result.relation = 'unknown'
        $result.lastAttempt = $null
        $result.issues = @($_.Exception.Message)
        return [pscustomobject]$result
    }
}
function Get-CheckpointStatus {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ModRoot)
    Get-CheckpointStatusCore -ModRoot $ModRoot
}
Export-ModuleMember -Function Get-CheckpointStatus
