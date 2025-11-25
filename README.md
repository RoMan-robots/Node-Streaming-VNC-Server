# Node Streaming VNC Server

[🇺🇦 Українська версія нижче](#node-streaming-vnc-server-ua)

High-performance native Node.js addon that implements a VNC server capable of streaming Windows 10/11 desktops and accepting input control.

## Features

- **High Performance**: Uses Windows Desktop Duplication API (DXGI) for GPU-accelerated screen capture.
- **Efficient**: Implements "Dirty Rectangles" detection to only send changed parts of the screen.
- **Resource Friendly**: Capture loop automatically stops when no clients are connected.
- **Standard Protocol**: Compatible with standard VNC clients (RFB) and WebSockets (noVNC).

## Requirements

- **OS**: Windows 10 or Windows 11 (x64).
- **Node.js**: Version 18.x or newer.
- **Build Tools**: Visual Studio 2019+ with C++ Desktop Development workload (for `node-gyp`).

## Installation

1.  Clone the code into your folder.
2.  Install dependencies and build the native addon:

```bash
npm install
```

This command will automatically run `node-gyp rebuild` to compile the C++ code.

## Usage

### TypeScript Example

```typescript
import { VncServer } from './src/main';

const server = new VncServer({
  port: 5902, // WebSocket port for noVNC
  password: 'password'
});

server.on('client-connected', (client) => {
  console.log(`Client connected: ${client.id}`);
});

server.on('client-disconnected', (client) => {
  console.log(`Client disconnected: ${client.id}`);
});

server.on('error', (err) => {
  console.error('Server error:', err);
});

function start() {
  try {
    server.start();
    console.log('VNC Server running on port 5902');
  } catch (err) {
    console.error('Failed to start:', err);
  }
}

start();
```

### Running the Example

```bash
npm start
```

Then open [noVNC](https://novnc.com/noVNC/vnc.html) and connect to:
- **Host**: `localhost`
- **Port**: `5902`
- **Password**: `password`

## API Documentation

### `VncServer`

#### `constructor(options: VncServerOptions)`
Creates a new server instance.

**Options:**
- `port` (number): The WebSocket port to listen on.
- `password` (string, optional): VNC password.

#### `start(): void`
Starts the server and begins listening for connections.

#### `stop(): void`
Stops the server and disconnects all clients.

#### `setQuality(options: QualityOptions): void`
Updates stream quality settings on the fly.

**Options:**
- `jpegQuality` (number): 0-100.
- `zlibLevel` (number): 0-9.

#### `getActiveClientsCount(): number`
Returns the number of currently connected clients.

## Architecture

- **Native Layer (`native/vnc_server.cc`)**: Handles low-level DXGI capture, thread management, and WinAPI input injection.
- **N-API**: Provides the bridge between C++ and Node.js.
- **TypeScript Layer (`src/main.ts`)**: Provides a high-level, type-safe API.

---

# Node Streaming VNC Server (UA)

Високопродуктивний нативний Node.js аддон, що реалізує VNC-сервер, здатний транслювати робочий стіл Windows 10/11 та приймати команди керування.

## Можливості

- **Висока продуктивність**: Використовує Windows Desktop Duplication API (DXGI) для захоплення екрана з апаратним прискоренням GPU.
- **Ефективність**: Реалізує виявлення "брудних прямокутників" (Dirty Rectangles), щоб надсилати лише змінені частини екрана.
- **Економія ресурсів**: Цикл захоплення автоматично зупиняється, коли немає підключених клієнтів.
- **Стандартний протокол**: Сумісний зі стандартними VNC-клієнтами (RFB) та WebSockets (noVNC).

## Вимоги

- **ОС**: Windows 10 або Windows 11 (x64).
- **Node.js**: Версія 18.x або новіша.
- **Інструменти збірки**: Visual Studio 2019+ з навантаженням "C++ Desktop Development" (для `node-gyp`).

## Встановлення

1.  Скопіюйте код у вашу папку.
2.  Встановіть залежності та зберіть нативний аддон:

```bash
npm install
```

Ця команда автоматично запустить `node-gyp rebuild` для компіляції C++ коду.

## Використання

### Приклад TypeScript

```typescript
import { VncServer } from './src/main';

const server = new VncServer({
  port: 5902, // WebSocket порт для noVNC
  password: 'password'
});

server.on('client-connected', (client) => {
  console.log(`Клієнт підключився: ${client.id}`);
});

server.on('client-disconnected', (client) => {
  console.log(`Клієнт відключився: ${client.id}`);
});

server.on('error', (err) => {
  console.error('Помилка сервера:', err);
});

function start() {
  try {
    server.start();
    console.log('VNC Сервер запущено на порту 5902');
  } catch (err) {
    console.error('Не вдалося запустити:', err);
  }
}

start();
```

### Запуск прикладу

```bash
npm start
```

Потім відкрийте [noVNC](https://novnc.com/noVNC/vnc.html) і підключіться до:
- **Host**: `localhost`
- **Port**: `5902`
- **Password**: `password`

## Документація API

### `VncServer`

#### `constructor(options: VncServerOptions)`
Створює новий екземпляр сервера.

**Параметри:**
- `port` (number): WebSocket порт для прослуховування.
- `password` (string, optional): Пароль VNC.

#### `start(): void`
Запускає сервер і починає слухати підключення.

#### `stop(): void`
Зупиняє сервер і відключає всіх клієнтів.

#### `setQuality(options: QualityOptions): void`
Оновлює налаштування якості "на льоту".

**Параметри:**
- `jpegQuality` (number): 0-100.
- `zlibLevel` (number): 0-9.

#### `getActiveClientsCount(): number`
Повертає кількість наразі підключених клієнтів.

## Архітектура

- **Нативний шар (`native/vnc_server.cc`)**: Обробляє низькорівневе захоплення DXGI, керування потоками та ін'єкцію вводу WinAPI.
- **N-API**: Забезпечує міст між C++ та Node.js.
- **TypeScript шар (`src/main.ts`)**: Надає високорівневий, типізований API.
