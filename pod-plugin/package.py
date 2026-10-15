#!/usr/bin/env python3
"""Create the POD 2 upload ZIP from an explicit, deployment-neutral file list."""
from pathlib import Path
import hashlib
import json
import re
import zipfile

ROOT = Path(__file__).resolve().parent
VERSION = '1.0.0'
FILES = [
    'extension.json', 'README.md', 'i18n/i18n.properties',
    'widget/MqttDashboard.js', 'widget/State.js', 'widget/Dashboard.js',
    'widget/Connection.js', 'lib/Mqtt.js', 'lib/MQTT-LICENSE.md',
    'assets/dashboard.css', 'assets/sap-logo.svg', 'assets/SOURCES.md',
    'config/factory.json', 'config/brewery.json', 'config/bakery.json',
    'config/ev_battery.json',
]


def package(output=None):
    manifest = json.loads((ROOT / 'extension.json').read_text())
    assert manifest['actions'] == []
    for widget in manifest['widgets']:
        assert widget['modulePath'].replace('/', '.') == widget['type']
        assert (ROOT / ('widget/' + widget['modulePath'].rsplit('/', 1)[1] + '.js')).is_file()
    for name in FILES:
        content = (ROOT / name).read_text()
        if re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY|github_pat_[A-Za-z0-9_]{20,}|gh[pousr]_[A-Za-z0-9]{20,}', content):
            raise ValueError('Credential material is not permitted in an extension package: ' + name)
    # Profiles and vendor bundle use the same canonical files as the firmware/web tools.
    firmware = ROOT.parent / 'firmware/main/spiffs_image'
    for target, source in [('factory','sapdm_config'), ('brewery','config_brewery'), ('bakery','config_bakery'), ('ev_battery','config_ev_battery')]:
        assert (ROOT / ('config/' + target + '.json')).read_bytes() == (firmware / (source + '.json')).read_bytes(), 'Profile copy is out of date'
    assert (ROOT.parent / 'dashboard/html/assets/mqtt.min.js').read_text() in (ROOT / 'lib/Mqtt.js').read_text(), 'Vendor bundle is out of date'
    output = Path(output) if output else ROOT / 'build' / f'dm-mqtt-pod2-v{VERSION}.zip'
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(FILES):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, (ROOT / name).read_bytes())
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        assert sorted(archive.namelist()) == sorted(FILES)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix('.zip.sha256').write_text(f'{digest}  {output.name}\n')
    print(f'Created {output.name}: {len(FILES)} files, {output.stat().st_size:,} bytes; SHA256 {digest}')
    return output


if __name__ == '__main__':
    package()
