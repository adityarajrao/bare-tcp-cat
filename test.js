const test = require('brittle')
const net = require('bare-net') // Change 'net' to 'bare-net' here
const tcpCat = require('./index')

test('basic tcpCat request and response', async (t) => {
    t.plan(2)

    const expectedResponse = 'Hello from the local TCP server!'
    
    // Create a local mock TCP server
    const server = net.createServer((socket) => {
        socket.on('data', (data) => {
            t.is(data.toString(), 'GET / HTTP/1.1\r\n\r\n')
            socket.write(expectedResponse)
            socket.end()
        })
    })

    // Start listening on a random port
    server.listen(0, '127.0.0.1', async () => {
        const address = server.address()
        
        try {
            // Perform the tcpCat query
            const response = await tcpCat('127.0.0.1', address.port, 'GET / HTTP/1.1\r\n\r\n')
            
            // Verify that the response is a Buffer and contains the expected string
            t.is(response.toString(), expectedResponse)
        } catch (err) {
            t.fail('tcpCat threw an unexpected error: ' + err.message)
        } finally {
            server.close()
        }
    })
})