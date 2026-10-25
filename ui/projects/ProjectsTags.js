.pragma library
var names = ["Housing", "Prototype", "Customer A", "R&D", "Quality Control", "Tutorial"]
var colors = ["#3996ed", "#aa6de3", "#ef963e", "#24c88d", "#edc94c", "#94a1aa"]
function color(name) {
    var index = names.indexOf(name)
    return index < 0 ? "#7fb8d0" : colors[index]
}
function count(rows, name) {
    return rows.filter(function(row) { return row.tag === name }).length
}
