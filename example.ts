import { VncServer } from './src/main';

const server = new VncServer({
    port: 5902,
    password: 'password'
});

server.on('client-connected', (client: any) => {
    console.log(`Client connected: ${client.id}`);
});

server.on('client-disconnected', (client: any) => {
    console.log(`Client disconnected: ${client.id}`);
});

server.on('error', (err: Error) => {
    console.error('VNC Server Error:', err);
});

function startServer() {
    try {
        server.start();
        console.log('VNC Server is running on port 5902!');
        console.log('Connect using noVNC at http://localhost:6080/vnc.html?host=localhost&port=5902');
    } catch (error) {
        console.error('Failed to start server:', error);
    }
}

startServer();

// Handle graceful shutdown
process.on('SIGINT', () => {
    console.log('\nStopping server...');
    server.stop();
    console.log('Server stopped.');
    process.exit(0);
});
