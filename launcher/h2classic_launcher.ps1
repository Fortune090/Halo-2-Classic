# H2Classic test launcher: tick the settings, press Launch. Only what is ticked here reaches the game:
# every H2N_ variable left over in the PowerShell window is cleared for the game it starts.
# Settings are remembered in h2classic\launcher.json next to this script.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
# (s10r) one program runs both game versions (1.0, and 1.5: the last title update, for the map packs); this launcher
# asks for the version through H2N_VERSION, which comes before the Settings program's choice
$exes = @{ '1.0' = 'H2Classic.exe'; '1.5' = 'H2Classic.exe' }
$saveDir = Join-Path $here 'h2classic'
$saveFile = Join-Path $saveDir 'launcher.json'

# name, kind (check / choice / text), label, value when ticked or list of choices, group; d = ticked from the start
# (for a text box: the value it starts with), until the box is unticked once (s10o: system link to xemu on this PC)
$items = @(
  @{ n='H2N_RENDER';    k='choice'; l='Render scale (x 640x480)';            v=@('1','2','3','4','5','6','7','8','9','10','11','12','13','14','15','16','17','18','19','20'); g='Picture' },
  @{ n='H2N_SCALE';     k='choice'; l='Window size (x 640x480)';             v=@('1','2','3');                     g='Picture' },
  @{ n='H2N_WIDE';      k='check';  l='Widescreen (16:9)';                   v='1';      g='Picture' },
  @{ n='H2N_BOOTSCREEN'; k='check'; l='First loading screen as the game draws it: LOADING n% (build 53)'; v='0'; g='Picture' },
  @{ n='H2N_UNLOCK';    k='check';  l='Unlock frame rate in a game';         v='1';      g='Picture' },
  @{ n='H2N_VSYNC';     k='check';  l='VSync off: frames as fast as the PC can make them'; v='off'; g='Picture' },
  @{ n='H2N_FPS';       k='check';  l='FPS counter'; v='1';      g='Picture' },
  @{ n='H2N_DISPLAY';   k='choice'; l='Window kind (s9w)';                   v=@('windowed','borderless','fullscreen'); g='Picture' },
  @{ n='H2N_WINDOW_SIZE'; k='text'; l='Window size in pixels, e.g. 2560x1440 (s9w)'; v='';   g='Picture' },
  @{ n='H2N_RENDERSCALE'; k='text'; l='Render scale % (before s10t; not with Render scale)'; v='';      g='Picture' },
  @{ n='H2N_GFXDEPTH';  k='check';  l="Stage 8's depth (for comparing)";     v='0';      g='Picture' },
  @{ n='H2N_SHADOWMAP'; k='check';  l='Lights near you without their shadow map (as build 32)'; v='0'; g='Picture' },
  @{ n='H2N_AA';        k='choice'; l='Anti-aliasing (SMAA, FXAA, TAA, DLSS)';   v=@('smaa','fxaa','taa','dlss'); g='Anti-aliasing' },
  @{ n='H2N_SMAA';      k='choice'; l='SMAA preset (unticked: High)';         v=@('low','medium','high','ultra'); g='Anti-aliasing' },
  @{ n='H2N_DLSS_PRESET'; k='choice'; l='DLSS preset (unticked: NVIDIA default)'; v=@('k','j');  g='Anti-aliasing' },
  @{ n='H2N_SSAA';      k='choice'; l='Supersampling (old window: see README)'; v=@('2','3','4'); g='Anti-aliasing' },
  @{ n='H2N_AADEBUG';   k='choice'; l='TAA/DLSS test view (not for playing)';  v=@('motion','error'); g='Anti-aliasing' },
  @{ n='H2N_DLSS_JITTER'; k='check'; l='DLSS test: the jitter the other way';  v='flip';   g='Anti-aliasing' },
  @{ n='H2N_DLSS_DIRECT'; k='check'; l='DLSS test: motion and depth unconverted'; v='1';   g='Anti-aliasing' },
  @{ n='H2N_SKIPINTRO'; k='check';  l='Skip the intro movie at start';       v='1';      g='Movies' },
  @{ n='H2N_SKIPATTRACT'; k='check'; l='Skip the attract movie when idle';    v='1';      g='Movies' },
  @{ n='H2N_MOVIES';    k='check';  l='No movies at all (as before s9s)';    v='off';    g='Movies' },
  @{ n='H2N_SPOOKY';     k='check'; l='Spooky Ghost Menu (Back + D-pad Up in the game)'; v='1'; g='Spooky Ghost' },
  @{ n='H2N_WIREFRAME';  k='check'; l='  Wireframe outlines every triangle (as the console; unticked: real edges only)'; v='triangles'; g='Spooky Ghost' },
  @{ n='H2N_CARRY_MINE'; k='check'; l='Effects on your own unit stay at the tick (as before build 32)'; v='0'; g='In-between frames (Unlock)' },
  @{ n='H2N_ROCKETFIX'; k='check';  l='Rocket launcher animation fix off (for comparing)'; v='0'; g='In-between frames (Unlock)' },
  @{ n='H2N_LODS';      k='check';  l='Increased LODs off (for comparing)';  v='0';      g='Unlock Performance' },
  @{ n='H2N_RUMBLE';    k='check';  l='No vibration (for comparing)';        v='0';      g='Controller' },
  @{ n='H2N_KBM';       k='check';  l='Keyboard + mouse off (s10v)';         v='0';      g='Controller' },
  @{ n='H2N_MOUSE_SENS'; k='text';  l='Mouse sensitivity (empty = 2.5)';     v='';       g='Controller' },
  @{ n='H2N_MOUSE_ZOOM'; k='text';  l='Mouse zoom sensitivity (empty = 1.0)'; v='';      g='Controller' },
  @{ n='H2N_MOUSE_INVERT'; k='check'; l='Mouse look inverted';               v='1';      g='Controller' },
  @{ n='H2N_NETWORK';   k='check';  l='No network (as before s9t)';          v='off';    g='Network' },
  @{ n='H2N_XNET';      k='check';  l='System link (the XBE''s own network library)'; v='retail'; g='Network'; d='on' },
  @{ n='H2N_XNET_PEER'; k='text';   l='  Tunnel only: send to (host:port)'; v=''; g='Network'; d='127.0.0.1:9368' },
  @{ n='H2N_XNET_PORT'; k='text';   l='  Tunnel only: listen on UDP port (empty = 9368)';  v='';       g='Network'; d='9369' },
  @{ n='H2N_XNET_LINK'; k='choice'; l='  How players are reached (unticked: found by themselves = lan)'; v=@('lan','tunnel'); g='Network' },
  @{ n='H2N_XNET_LOCAL'; k='check'; l='  This PC only (nothing is sent to the network)'; v='1'; g='Network' },
  @{ n='H2N_XNET_MAC';  k='text';   l='  Console address (aa:bb:cc:dd:ee:ff; empty = made from this PC)'; v='';  g='Network' },
  @{ n='H2N_WATCH';     k='text';   l='Watch log: every N ticks (e.g. 30)';  v='';       g='Logging' },
  @{ n='H2N_WATCHOBJ';  k='check';  l='Watch log: falling objects too';      v='1';      g='Logging' },
  @{ n='H2N_DUMP';      k='check';  l='Stop file holds all game memory (55 MB)'; v='all'; g='Logging' },
  @{ n='H2N_SECONDS';   k='text';   l='Stop by itself after N seconds';      v='';       g='Logging' },
  @{ n='H2N_AADIAG';    k='check';  l='AA log: the first 30 depth-tested draws'; v='1';    g='Logging' }
)

$saved = @{}
if (Test-Path $saveFile) {
  try { (Get-Content $saveFile -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $saved[$_.Name] = [string]$_.Value } } catch { }
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'H2Classic test launcher'
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9)

$controls = @{}
$y = 10
$lastGroup = ''
foreach ($it in $items) {
  if ($it.g -ne $lastGroup) {
    $h = New-Object System.Windows.Forms.Label
    $h.Text = $it.g
    $h.Font = New-Object System.Drawing.Font('Segoe UI', 9, [System.Drawing.FontStyle]::Bold)
    $h.Location = New-Object System.Drawing.Point(12, $y)
    $h.AutoSize = $true
    $form.Controls.Add($h)
    $y += 24
    $lastGroup = $it.g
  }
  $cb = New-Object System.Windows.Forms.CheckBox
  $cb.Text = $it.l
  $cb.Location = New-Object System.Drawing.Point(24, $y)
  $cb.Size = New-Object System.Drawing.Size(290, 22)
  $form.Controls.Add($cb)
  $edit = $null
  if ($it.k -eq 'choice') {
    $edit = New-Object System.Windows.Forms.ComboBox
    $edit.DropDownStyle = 'DropDownList'
    [void]$edit.Items.AddRange($it.v)
    $edit.SelectedIndex = 0
  } elseif ($it.k -eq 'text') {
    $edit = New-Object System.Windows.Forms.TextBox
  }
  if ($edit) {
    $edit.Location = New-Object System.Drawing.Point(320, $y)
    $edit.Size = New-Object System.Drawing.Size(150, 22)
    $form.Controls.Add($edit)
  }
  if ($saved.ContainsKey($it.n)) {
    $cb.Checked = $true
    if ($it.k -eq 'choice' -and $it.v -contains $saved[$it.n]) { $edit.SelectedItem = $saved[$it.n] }
    if ($it.k -eq 'text') { $edit.Text = $saved[$it.n] }
  }
  if ($it.d -and -not $saved.ContainsKey($it.n) -and -not $saved.ContainsKey($it.n + '_value') -and -not $saved.ContainsKey($it.n + '_off')) {
    $cb.Checked = $true
    if ($it.k -eq 'text') { $edit.Text = $it.d }
  }
  if ($saved.ContainsKey($it.n + '_value') -and -not $saved.ContainsKey($it.n)) {
    if ($it.k -eq 'choice' -and $it.v -contains $saved[$it.n + '_value']) { $edit.SelectedItem = $saved[$it.n + '_value'] }
    if ($it.k -eq 'text') { $edit.Text = $saved[$it.n + '_value'] }
  }
  $controls[$it.n] = @{ check = $cb; edit = $edit; item = $it }
  $y += 26
}

$y += 6
$verLabel = New-Object System.Windows.Forms.Label
$verLabel.Text = 'Game version (1.5 = the title update, for the map packs)'
$verLabel.Location = New-Object System.Drawing.Point(12, ($y + 3))
$verLabel.AutoSize = $true
$form.Controls.Add($verLabel)
$version = New-Object System.Windows.Forms.ComboBox
$version.DropDownStyle = 'DropDownList'
[void]$version.Items.AddRange(@('1.0', '1.5'))
$version.SelectedIndex = 0
if ($saved.ContainsKey('_version') -and $exes.ContainsKey($saved['_version'])) { $version.SelectedItem = $saved['_version'] }
$version.Location = New-Object System.Drawing.Point(320, $y)
$version.Size = New-Object System.Drawing.Size(150, 22)
$form.Controls.Add($version)
$y += 30
$isoLabel = New-Object System.Windows.Forms.Label
$isoLabel.Text = 'Game folder or .iso (empty = the Roms folder here)'
$isoLabel.Location = New-Object System.Drawing.Point(12, ($y + 3))
$isoLabel.AutoSize = $true
$form.Controls.Add($isoLabel)
$y += 24
$iso = New-Object System.Windows.Forms.TextBox
$iso.Location = New-Object System.Drawing.Point(24, $y)
$iso.Size = New-Object System.Drawing.Size(446, 22)
if ($saved.ContainsKey('_path')) { $iso.Text = $saved['_path'] }
$form.Controls.Add($iso)
$y += 32

$summary = New-Object System.Windows.Forms.TextBox
$summary.Multiline = $true
$summary.ReadOnly = $true
$summary.Location = New-Object System.Drawing.Point(12, $y)
$summary.Size = New-Object System.Drawing.Size(458, 54)
$form.Controls.Add($summary)
$y += 62

function Get-Active {
  $active = [ordered]@{}
  foreach ($it in $items) {
    $c = $controls[$it.n]
    if (-not $c.check.Checked) { continue }
    $value = $it.v
    if ($it.k -eq 'choice') { $value = [string]$c.edit.SelectedItem }
    if ($it.k -eq 'text') { $value = $c.edit.Text.Trim() }
    if ($value -ne '') { $active[$it.n] = $value }
  }
  return $active
}
function Update-Summary {
  $a = Get-Active
  if ($a.Count -eq 0) { $summary.Text = 'Active: nothing (the build as it is)' }
  else { $summary.Text = 'Active: ' + (($a.GetEnumerator() | ForEach-Object { $_.Key + '=' + $_.Value }) -join '   ') }
}
foreach ($c in $controls.Values) {
  $c.check.Add_CheckedChanged({ Update-Summary })
  if ($c.edit -is [System.Windows.Forms.ComboBox]) { $c.edit.Add_SelectedIndexChanged({ Update-Summary }) }
  if ($c.edit -is [System.Windows.Forms.TextBox]) { $c.edit.Add_TextChanged({ Update-Summary }) }
}

$launch = New-Object System.Windows.Forms.Button
$launch.Text = 'Launch'
$launch.Location = New-Object System.Drawing.Point(12, $y)
$launch.Size = New-Object System.Drawing.Size(110, 30)
$form.Controls.Add($launch)
$clear = New-Object System.Windows.Forms.Button
$clear.Text = 'Untick all'
$clear.Location = New-Object System.Drawing.Point(132, $y)
$clear.Size = New-Object System.Drawing.Size(110, 30)
$form.Controls.Add($clear)
$status = New-Object System.Windows.Forms.Label
$status.Location = New-Object System.Drawing.Point(252, ($y + 7))
$status.Size = New-Object System.Drawing.Size(220, 20)
$form.Controls.Add($status)
$y += 42
# (s10v) more options than a 1440-line screen is tall: the window then stops at the screen's height and scrolls
$work = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
if (($y + 48) -gt $work.Height) {
  $form.AutoScroll = $true
  $form.ClientSize = New-Object System.Drawing.Size(506, ($work.Height - 48))
} else {
  $form.ClientSize = New-Object System.Drawing.Size(484, $y)
}

$clear.Add_Click({ foreach ($c in $controls.Values) { $c.check.Checked = $false } })

$launch.Add_Click({
  $ver = [string]$version.SelectedItem
  $exe = Join-Path $here $exes[$ver]
  if (-not (Test-Path $exe)) { $status.Text = $exes[$ver] + ' is not in this folder'; return }
  $a = Get-Active
  # remember: ticked settings by name; values of unticked boxes too, so they come back
  $keep = [ordered]@{}
  foreach ($k in $a.Keys) { $keep[$k] = $a[$k] }
  foreach ($it in $items) {
    $c = $controls[$it.n]
    if (-not $c.check.Checked) {
      if ($it.d) { $keep[$it.n + '_off'] = '1' }          # unticked on purpose: it stays unticked next time
      if ($it.k -eq 'choice') { $keep[$it.n + '_value'] = [string]$c.edit.SelectedItem }
      if ($it.k -eq 'text' -and $c.edit.Text.Trim() -ne '') { $keep[$it.n + '_value'] = $c.edit.Text.Trim() }
    }
  }
  if ($iso.Text.Trim() -ne '') { $keep['_path'] = $iso.Text.Trim() }
  $keep['_version'] = $ver
  try {
    if (-not (Test-Path $saveDir)) { [void](New-Item -ItemType Directory -Path $saveDir) }
    ($keep | ConvertTo-Json) | Set-Content -Path $saveFile -Encoding UTF8
  } catch { }
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe
  $psi.WorkingDirectory = $here
  $psi.UseShellExecute = $false
  if ($iso.Text.Trim() -ne '') { $psi.Arguments = '"' + $iso.Text.Trim() + '"' }
  foreach ($name in @($psi.EnvironmentVariables.Keys)) { if ($name -like 'H2N_*') { $psi.EnvironmentVariables.Remove($name) } }
  foreach ($k in $a.Keys) { $psi.EnvironmentVariables[$k] = $a[$k] }
  # the test launcher's own settings stand in for h2classic\settings.ini: the game does not open the release launcher
  $psi.EnvironmentVariables['H2N_NOLAUNCHER'] = '1'
  # and the release launcher's h2classic\settings.ini is not read: what is ticked here is the whole set (s10f)
  $psi.EnvironmentVariables['H2N_SETTINGS'] = 'off'
  # (s10r) the game version chosen here
  $psi.EnvironmentVariables['H2N_VERSION'] = $ver
  try {
    [void][System.Diagnostics.Process]::Start($psi)
    $status.Text = 'Started ' + $ver + ' ' + (Get-Date -Format 'HH:mm:ss')
  } catch { $status.Text = 'Could not start: ' + $_.Exception.Message }
})

Update-Summary
[void]$form.ShowDialog()
