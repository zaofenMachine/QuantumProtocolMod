#Requires -Version 7.2
[CmdletBinding(DefaultParameterSetName = 'GameRoot')]
param(
    [Parameter(ParameterSetName = 'GameRoot')][string]$GameRoot = 'F:\SteamLibrary\steamapps\common\Quantum Protocol',
    [Parameter(Mandatory,ParameterSetName = 'ModRoot')][string]$ModRoot,
    [switch]$Json
)
$ErrorActionPreference = 'Stop'
function Safe-StatusText([string]$Text) {
    return [regex]::Replace($Text,'[\x00-\x1f\x7f-\x9f\u200b-\u200f\u202a-\u202e\u2066-\u2069]', {
        param($m)'\u'+([int][char]$m.Value).ToString('x4')
    })
}
try {
    Import-Module (Join-Path $PSScriptRoot 'CheckpointStatus.psm1') -Force -ErrorAction Stop
    if ($PSCmdlet.ParameterSetName -eq 'GameRoot') {
        $ModRoot = Join-Path ([IO.Path]::GetFullPath($GameRoot)) 'Quantum/Binaries/Win64/Mods/QuantumCheckpoint'
    }
    if (![IO.Path]::IsPathFullyQualified($ModRoot) -or $ModRoot.StartsWith('\\') -or $ModRoot.StartsWith('//')) {
        throw [ArgumentException]::new('ModRoot必须为本地绝对路径。')
    }
    $status = Get-CheckpointStatus -ModRoot $ModRoot
    if ($Json) {
        $status|ConvertTo-Json -Depth 40
    }
    else {
        if ($null -ne $status.lastAttempt) {
            $label = if (!$status.lastAttempt.diagnosticComplete) {
                '诊断不完整，无法确认保存结果（原记录 outcome='+(Safe-StatusText $status.lastAttempt.outcome)+'）'
            }
            else {
                switch ($status.lastAttempt.outcome) {
                    'rejected-before-main' {
                        '已拒绝，未发布新主档'
                    }
                    'main-only' {
                        '已记录小关重开主档；玩家状态未成套保存'
                    }
                    'player-bundle-committed' {
                        '已记录玩家布局与资格依赖写入'
                    }
                    default {
                        '记录显示部分发布'
                    }
                }
            }
            Write-Output ('回执记录：'+$label+'（'+(Safe-StatusText $status.lastAttempt.attemptId)+'，'+(Safe-StatusText $status.lastAttempt.finishedAtUtc)+'）')
            $failure = if ($status.lastAttempt.firstPlayerRefusal) {
                $status.lastAttempt.firstPlayerRefusal
            }
            else {
                $status.lastAttempt.firstFailure
            }
            if ($failure) {
                Write-Output ('原始原因：'+(Safe-StatusText $failure.reason))
            }
        }
        else {
            Write-Output ('上次保存尝试：'+$(if ($status.receiptStatus -eq 'missing') {
                '没有回执'
            }
            else {
                '回执无法确认'
            }))
        }
        $relation = switch ($status.relation) {
            'matching-files' {
                '主档及已记录部件字节对应'
            }
            'no-publication' {
                '本次未发布；当前来源未由回执证明'
            }
            'stale' {
                '回执已过期，当前主档不同或缺失'
            }
            'incomplete' {
                '主档对应，部分依赖／路径不一致或不明'
            }
            default {
                '对应关系不明'
            }
        }
        Write-Output ('当前检查点：'+$relation)
        $names = @{
            main = '主档'
            spawn = '未来生成'
            hand = '手牌状态'
            qualification = '玩家资格'
            layout = '玩家布局'
            charge = '角色资源'
            turn = '回合进度'
            zones = '牌区布局'
            trash = '墓地布局'
            field = '场地布局'
        }
        $states = @{
            'present-unbound' = '文件存在但未由此回执关联'
            absent = '不存在'
            missing = '文件缺失'
            changed = '文件与回执不同'
            unbound = '没有可核对哈希'
            'path-mismatch' = '原记录路径不同'
            prepared = '尚未发布'
            writing = '写入结果未确认'
            failed = '发布失败'
            withdrawn = '已撤回，不能据此推断文件删除'
            'unknown-layout' = '布局类型未知'
        }
        foreach ($c in $status.components) {
            if ($c.currentStatus -notin @('matching','not-recorded')) {
                Write-Output ('部件 '+$names[$c.name]+'：'+$states[$c.currentStatus])
            }
        }
        foreach ($issue in $status.issues) {
            Write-Output ('说明：'+(Safe-StatusText $issue))
        }
        Write-Output '仅查看历史保存记录与两次采样文件比对；不判断恢复准入，也不证明完整玩家状态。'
    }
    exit $status.exitCode
}
catch {
    Write-Error ('状态查看失败：'+(Safe-StatusText $_.Exception.Message)) -ErrorAction Continue
    exit 1
}
