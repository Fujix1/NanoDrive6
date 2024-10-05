var color565 = 0x213F;
var R5 = (color565 >> 11) & 0x1f;
var G6 = (color565 >> 5) & 0x3f;
var B5 = (color565) & 0x1f;
var R8 = ( R5 * 527 + 23 ) >> 6;
var G8 = ( G6 * 259 + 33 ) >> 6;
var B8 = ( B5 * 527 + 23 ) >> 6;

var R8S = ((R8>=0x10)?"0x":"0x0") + R8.toString(16);
var G8S = ((G8>=0x10)?"":"0") + G8.toString(16);
var B8S = ((B8>=0x10)?"":"0") + B8.toString(16);

console.log(R8S+G8S+B8S)
