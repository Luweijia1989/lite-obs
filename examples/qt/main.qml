import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.5
import com.ypp 1.0

Item {
    visible: true

    Render {
        id: renderer
        anchors.fill: parent
        anchors.margins: 10

        // The transform is just to show something interesting..
        transform: [
            Rotation { id: rotation; axis.x: 0; axis.z: 0; axis.y: 1; angle: 0; origin.x: renderer.width / 2; origin.y: renderer.height / 2; },
            Translate { id: txOut; x: -renderer.width / 2; y: -renderer.height / 2 },
            Scale { id: scale; },
            Translate { id: txIn; x: renderer.width / 2; y: renderer.height / 2 }
        ]

        onNewTexture: function(texId, texWidth, texHeight) {
            example.doTextureMix(texId, texWidth, texHeight)
        }
    }

    // Just to show something interesting
    SequentialAnimation {
        PauseAnimation { duration: 5000 }
        ParallelAnimation {
            NumberAnimation { target: scale; property: "xScale"; to: 0.6; duration: 1000; easing.type: Easing.InOutBack }
            NumberAnimation { target: scale; property: "yScale"; to: 0.6; duration: 1000; easing.type: Easing.InOutBack }
        }
        NumberAnimation { target: rotation; property: "angle"; to: 80; duration: 1000; easing.type: Easing.InOutCubic }
        NumberAnimation { target: rotation; property: "angle"; to: -80; duration: 1000; easing.type: Easing.InOutCubic }
        NumberAnimation { target: rotation; property: "angle"; to: 0; duration: 1000; easing.type: Easing.InOutCubic }
        NumberAnimation { target: renderer; property: "opacity"; to: 0.5; duration: 1000; easing.type: Easing.InOutCubic }
        PauseAnimation { duration: 1000 }
        NumberAnimation { target: renderer; property: "opacity"; to: 0.8; duration: 1000; easing.type: Easing.InOutCubic }
        ParallelAnimation {
            NumberAnimation { target: scale; property: "xScale"; to: 1; duration: 1000; easing.type: Easing.InOutBack }
            NumberAnimation { target: scale; property: "yScale"; to: 1; duration: 1000; easing.type: Easing.InOutBack }
        }
        running: true
        loops: Animation.Infinite
    }

    ColumnLayout {
        anchors.right: parent.right
        Button {
            text: "move"
            onClicked: {
                example.move()
            }
        }

        Button {
            text: "scale"
            onClicked: {
                example.scale()
            }
        }

        Button {
            text: "flip"
            onClicked: {
                example.flip()
            }
        }

        Button {
            text: "rotate"
            onClicked: {
                example.rotate()
            }
        }

        Button {
            text: "reset"
            onClicked: {
                example.reset();
            }
        }
    }

    ColumnLayout {
        Button {
            text: "mix audio"
            checkable: true
            onClicked: {
                example.doAudioMixTest(checked)
            }
        }

        Button {
            text: "mix video frame"
            checkable: true
            onClicked: {
                example.doVideoFrameMixTest(checked)
            }
        }

        Button {
            text: "mix img"
            checkable: true
            onClicked: {
                example.doImgMix(checked)
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

        Button {
            text: "up"
            onClicked: {
                example.setSourceOrder(0)
            }
        }

        Button {
            text: "down"
            onClicked: {
                example.setSourceOrder(1)
            }
        }

        Button {
            text: "top"
            onClicked: {
                example.setSourceOrder(2)
            }
        }

        Button {
            text: "bottom"
            onClicked: {
                example.setSourceOrder(3)
            }
        }

        Button {
            text: "soft"
            onClicked: {
                example.resetEncoderType(true)
            }
        }

        Button {
            text: "hw"
            onClicked: {
                example.resetEncoderType(false)
            }
        }
    }
}
