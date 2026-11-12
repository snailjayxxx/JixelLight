.pragma library

// Only the temporary screen guide is simplified. Every tablet packet still
// reaches the controller, including its unchanged pressure and image position.
function appendVisualPoint(points, x, y) {
    if (!Number.isFinite(x) || !Number.isFinite(y)) return null
    if (points.length > 0) {
        const last = points[points.length - 1]
        const dx = x - last.x, dy = y - last.y
        // Distance is from the last retained logical-pixel point, so small
        // movements accumulate instead of disappearing indefinitely.
        if (dx * dx + dy * dy < 0.25) return null
    }
    let next = points.slice()
    if (next.length >= 1024) next = next.filter(function(p, i) { return i % 2 === 0 })
    next.push({x: x, y: y})
    return next
}
