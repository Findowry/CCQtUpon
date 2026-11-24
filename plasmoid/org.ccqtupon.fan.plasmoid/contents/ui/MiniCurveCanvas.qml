import QtQuick
import org.kde.kirigami as Kirigami

Canvas {
    id: curveCanvas

    property var profile: null
    property var liveTemp: null
    property var liveDuty: null

    implicitHeight: Kirigami.Units.gridUnit * 4.5
    implicitWidth: Kirigami.Units.gridUnit * 12

    onProfileChanged: requestPaint()
    onLiveTempChanged: requestPaint()
    onLiveDutyChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function colorWithAlpha(c, alpha) {
        return Qt.rgba(c.r, c.g, c.b, alpha)
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)

        if (width < 40 || height < 30) {
            return
        }

        const padL = 28
        const padR = 10
        const padT = 8
        const padB = 18
        const plotW = width - padL - padR
        const plotH = height - padT - padB
        if (plotW <= 10 || plotH <= 10) {
            return
        }

        const accent = Kirigami.Theme.highlightColor
        const textCol = Kirigami.Theme.textColor
        const borderCol = colorWithAlpha(textCol, 0.22)
        const gridCol = colorWithAlpha(textCol, 0.12)
        const mutedCol = colorWithAlpha(textCol, 0.65)

        // Frame
        ctx.fillStyle = colorWithAlpha(Kirigami.Theme.backgroundColor, 0.45)
        ctx.strokeStyle = borderCol
        ctx.lineWidth = 1
        ctx.beginPath()
        ctx.roundedRect(padL, padT, plotW, plotH, 4, 4)
        ctx.fill()
        ctx.stroke()

        // Horizontal grid & Y ticks (0%, 50%, 100%)
        ctx.font = Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize > 0 ? Kirigami.Theme.smallFont.pixelSize : 10)) + "px sans-serif"
        ctx.fillStyle = mutedCol
        ctx.textAlign = "right"
        ctx.textBaseline = "middle"
        const yTicks = [0, 50, 100]
        for (let i = 0; i < yTicks.length; ++i) {
            const d = yTicks[i]
            const y = padT + plotH - (d / 100.0) * plotH
            if (d === 50) {
                ctx.strokeStyle = gridCol
                ctx.beginPath()
                ctx.moveTo(padL, y)
                ctx.lineTo(padL + plotW, y)
                ctx.stroke()
            }
            ctx.fillText(d + "%", padL - 4, y)
        }

        if (!profile) {
            return
        }

        const pType = profile.p_type || "Default"

        if (pType === "Fixed") {
            const fixedVal = Math.max(0, Math.min(100, profile.speed_fixed !== undefined && profile.speed_fixed !== null ? profile.speed_fixed : 50))
            const fy = padT + plotH - (fixedVal / 100.0) * plotH
            ctx.fillStyle = colorWithAlpha(accent, 0.18)
            ctx.fillRect(padL + 1, fy, plotW - 2, padT + plotH - fy)
            ctx.strokeStyle = accent
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(padL, fy)
            ctx.lineTo(padL + plotW, fy)
            ctx.stroke()
            ctx.fillStyle = textCol
            ctx.textAlign = "center"
            ctx.textBaseline = "bottom"
            ctx.fillText(i18n("Fixed: %1%", fixedVal), padL + plotW * 0.5, fy - 3)
            return
        }

        if (pType !== "Graph" || !profile.speed_profile || profile.speed_profile.length === 0) {
            ctx.fillStyle = mutedCol
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.fillText(pType, padL + plotW * 0.5, padT + plotH * 0.5)
            return
        }

        const pts = profile.speed_profile.slice().sort(function(a, b) {
            return a[0] - b[0]
        })

        let tMin = (profile.temp_min !== undefined && profile.temp_min !== null) ? profile.temp_min : 0
        let tMax = (profile.temp_max !== undefined && profile.temp_max !== null) ? profile.temp_max : 100
        for (let i = 0; i < pts.length; ++i) {
            if (pts[i][0] < tMin) tMin = pts[i][0]
            if (pts[i][0] > tMax) tMax = pts[i][0]
        }
        if (tMax <= tMin) {
            tMax = tMin + 10
        }

        function xForTemp(t) {
            const norm = Math.max(0, Math.min(1, (t - tMin) / (tMax - tMin)))
            return padL + norm * plotW
        }
        function yForDuty(d) {
            const norm = Math.max(0, Math.min(1, d / 100.0))
            return padT + plotH - norm * plotH
        }

        // X min/max labels
        ctx.fillStyle = mutedCol
        ctx.textAlign = "left"
        ctx.textBaseline = "top"
        ctx.fillText(Math.round(tMin) + "°C", padL, padT + plotH + 3)
        ctx.textAlign = "right"
        ctx.fillText(Math.round(tMax) + "°C", padL + plotW, padT + plotH + 3)

        // Under-curve fill
        ctx.fillStyle = colorWithAlpha(accent, 0.22)
        ctx.beginPath()
        ctx.moveTo(padL, padT + plotH)
        ctx.lineTo(padL, yForDuty(pts[0][1]))
        for (let i = 0; i < pts.length; ++i) {
            ctx.lineTo(xForTemp(pts[i][0]), yForDuty(pts[i][1]))
        }
        ctx.lineTo(padL + plotW, yForDuty(pts[pts.length - 1][1]))
        ctx.lineTo(padL + plotW, padT + plotH)
        ctx.closePath()
        ctx.fill()

        // Curve line
        ctx.strokeStyle = accent
        ctx.lineWidth = 2
        ctx.beginPath()
        ctx.moveTo(padL, yForDuty(pts[0][1]))
        for (let i = 0; i < pts.length; ++i) {
            ctx.lineTo(xForTemp(pts[i][0]), yForDuty(pts[i][1]))
        }
        ctx.lineTo(padL + plotW, yForDuty(pts[pts.length - 1][1]))
        ctx.stroke()

        // Control points
        for (let i = 0; i < pts.length; ++i) {
            const px = xForTemp(pts[i][0])
            const py = yForDuty(pts[i][1])
            ctx.fillStyle = Kirigami.Theme.backgroundColor
            ctx.strokeStyle = accent
            ctx.lineWidth = 1.6
            ctx.beginPath()
            ctx.arc(px, py, 3.0, 0, Math.PI * 2)
            ctx.fill()
            ctx.stroke()
        }

        // Live temp vertical dashed line & live duty horizontal dashed line
        const warmCol = Kirigami.Theme.neutralTextColor
        if (liveTemp !== null && liveTemp !== undefined) {
            const lx = xForTemp(liveTemp)
            ctx.save()
            ctx.strokeStyle = warmCol
            ctx.lineWidth = 1.3
            ctx.setLineDash([4, 3])
            ctx.beginPath()
            ctx.moveTo(lx, padT)
            ctx.lineTo(lx, padT + plotH)
            ctx.stroke()
            ctx.restore()
        }
        if (liveDuty !== null && liveDuty !== undefined) {
            const ly = yForDuty(liveDuty)
            ctx.save()
            ctx.strokeStyle = colorWithAlpha(accent, 0.75)
            ctx.lineWidth = 1.2
            ctx.setLineDash([4, 3])
            ctx.beginPath()
            ctx.moveTo(padL, ly)
            ctx.lineTo(padL + plotW, ly)
            ctx.stroke()
            ctx.restore()
        }
        if (liveTemp !== null && liveTemp !== undefined && liveDuty !== null && liveDuty !== undefined) {
            ctx.fillStyle = warmCol
            ctx.beginPath()
            ctx.arc(xForTemp(liveTemp), yForDuty(liveDuty), 3.6, 0, Math.PI * 2)
            ctx.fill()
        }
    }
}
