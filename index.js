const binding = require('./binding')
module.exports = async function tcpCat(ip, port, message) {
    // 1. Await the native promise resolving to an ArrayBuffer
    const arrayBuffer = await binding.tcpCat(ip, port, message)
    
    // 2. Wrap the ArrayBuffer in a Buffer object without copying memory
    return Buffer.from(arrayBuffer)
}