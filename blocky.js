"use strict";

function onLoad() {
    var canvas = document.getElementById('blocky');
    var ctx = canvas.getContext('2d');
    console.log("hi");
    ctx.fillStyle = 'rgb(200, 0, 0)';
    ctx.fillRect(10, 10, 50, 50);
}
