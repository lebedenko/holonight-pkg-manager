"""Each mutation must fail independently against the real application policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
checker = root / "scripts/check-runtime-qml-imports.sh"
with tempfile.TemporaryDirectory() as directory:
    fixture = Path(directory)
    shutil.copytree(root / "qml", fixture / "qml")
    def check(expected):
        result = subprocess.run(["bash", str(checker), str(fixture)], capture_output=True, text=True)
        assert (result.returncode == 0) == expected, result.stdout + result.stderr
    check(True)
    path = fixture / "qml/PolicyFixture.qml"
    for source in [
        "import Holonight as H\nH.Button {}",
        "import QtQuick.Controls.Fusion as F\nF.Button {}",
        "import QtQuick\nItem { property color tone: HoloniightPalette.background }",
        "import QtQuick\nHnSearchField {}",
        "import QtQuick.Controls as Controls\nDialog {}",
        "import Holonight.Controls\nHnListDelegate { selectionStyle: HnSelectableDelegate.Outline }",
        "import QtQuick.Controls.Basic as B\nB.Button {}",
        "import QtQuick.Controls\nButton {}",
        "import QtQuick.Controls as Wrong\nWrong.Button {}",
        "import QtQuick.Controls as Controls\nButton {}",
        "import QtQuick.Controls as Controls\nItem { property int p: Popup.CloseOnEscape }",
        "import QtQuick.Controls as Controls\nItem { ScrollBar.vertical: Controls.ScrollBar {} }",
        "import QtQuick\nControls.Button {}",
    ]:
        path.write_text(source + "\n")
        check(False)
    path.unlink()
    check(True)
    # The check bar has its own rules on top of the shared ones; each mutation must fail on its own.
    bar = fixture / "qml/updates/UpdatesCheckBar.qml"
    original = bar.read_text()
    for old, new in [
        ("Controls.Button {", "Button {"),
        ("import QtQuick.Controls as Controls", "import QtQuick.Controls"),
        ("import QtQuick.Controls as Controls", "import QtQuick.Controls as Wrong"),
        ("objectName: \"updatesCheckNowButton\"", "objectName: \"updatesCheckNowButton\"\n            Controls.ToolTip.visible: hovered"),
        ("objectName: \"updatesCheckNowButton\"", "objectName: \"updatesCheckNowButton\"\n            Controls.Popup {}"),
        ("Controls.ProgressBar {", "Controls.Dialog {"),
        ("Controls.Button {", "Controls.Menu {"),
    ]:
        assert old in original, old
        bar.write_text(original.replace(old, new, 1))
        check(False)
    bar.write_text(original)
    check(True)
print("Independent runtime import policy fixtures passed")
