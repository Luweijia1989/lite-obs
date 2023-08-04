import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.5

Item {
    width: 640
    height: 480
    visible: true

    RowLayout {
        anchors.centerIn: parent

        Button {
            text: "mix audio"
            checkable: true
            onClicked: {
                example.doAudioMixTest(checked)
            }
        }

        Button {
            text: "start output"
            onClicked: {
                example.doStartOutput()
            }
        }

        Button {
            text: "stop output"
            onClicked: {
                example.doStopOutput()
            }
        }
    }
}
