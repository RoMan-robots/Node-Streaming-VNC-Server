#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <napi.h>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincrypt.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "crypt32.lib")
#endif

// --- Constants & Helpers ---

const int RFB_SCREEN_W = 1920;
const int RFB_SCREEN_H = 1080;
const int BYTES_PER_PIXEL = 4;

struct Rect {
  int x, y, w, h;
};

#ifdef _WIN32
// SHA1 + Base64 helpers for WebSocket handshake
std::string ComputeSHA1Base64(const std::string &input) {
  std::string magic = input + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

  // SHA1 Hash
  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;
  BYTE hash[20];
  DWORD hashLen = 20;

  CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
  CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash);
  CryptHashData(hHash, (BYTE *)magic.c_str(), magic.length(), 0);
  CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
  CryptDestroyHash(hHash);
  CryptReleaseContext(hProv, 0);

  // Base64 Encode
  DWORD b64Len = 0;
  CryptBinaryToStringA(hash, hashLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                       NULL, &b64Len);
  std::vector<char> b64(b64Len);
  CryptBinaryToStringA(hash, hashLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                       b64.data(), &b64Len);

  return std::string(b64.data(), b64Len - 1); // -1 to remove null terminator
}
#endif

// Simple ThreadSafe Queue for broadcasting updates
template <typename T> class SafeQueue {
  std::queue<T> q;
  std::mutex m;
  std::condition_variable cv;

public:
  void push(T val) {
    std::lock_guard<std::mutex> lock(m);
    q.push(val);
    cv.notify_one();
  }
  bool pop(T &val, int timeoutMs = 0) {
    std::unique_lock<std::mutex> lock(m);
    if (timeoutMs > 0) {
      if (!cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                       [this] { return !q.empty(); }))
        return false;
    } else {
      cv.wait(lock, [this] { return !q.empty(); });
    }
    val = q.front();
    q.pop();
    return true;
  }
};

// --- VNC Server Class Definition ---

class VncServer : public Napi::ObjectWrap<VncServer> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  VncServer(const Napi::CallbackInfo &info);
  ~VncServer();

private:
  static Napi::FunctionReference constructor;

  // JS Methods
  Napi::Value Start(const Napi::CallbackInfo &info);
  Napi::Value Stop(const Napi::CallbackInfo &info);
  Napi::Value SetQuality(const Napi::CallbackInfo &info);
  Napi::Value GetActiveClientsCount(const Napi::CallbackInfo &info);

  // Events
  Napi::Value OnClientConnected(const Napi::CallbackInfo &info);
  Napi::Value OnClientDisconnected(const Napi::CallbackInfo &info);
  Napi::Value OnError(const Napi::CallbackInfo &info);

  // Core Logic
  void CaptureLoop();
  void NetworkLoop();
  void ClientHandler(uintptr_t socket, std::string id);

  // Helpers
#ifdef _WIN32
  void InitializeDXGI();
  void CleanupDXGI();
  bool AcquireFrame(std::vector<uint8_t> &buffer, int &width, int &height,
                    std::vector<Rect> &dirtyRects);
  void SendInput(int type, int x, int y, int button, int key);

  // WebSocket & RFB Helpers
  bool HandshakeWebSocket(SOCKET clientSocket);
  bool HandshakeRFB(SOCKET clientSocket, int width, int height,
                    std::string name);
  void SendFrameUpdate(SOCKET clientSocket, const std::vector<Rect> &rects,
                       const std::vector<uint8_t> &framebuffer, int fbWidth,
                       int fbHeight);
#endif

  // State
  std::atomic<bool> running;
  std::atomic<bool> captureRunning;
  std::thread networkThread;
  std::thread captureThread;

  // TSFNs
  Napi::ThreadSafeFunction onConnectTsfn;
  Napi::ThreadSafeFunction onDisconnectTsfn;
  Napi::ThreadSafeFunction onErrorTsfn;

  // Configuration
  int port;
  std::string password;

  std::atomic<int> activeClients;

  // Screen dimensions (set from DXGI)
  int width = 1920;
  int height = 1080;

  // Framebuffer State (Shared between Capture and Clients)
  std::vector<uint8_t> serverFramebuffer;
  std::mutex framebufferMutex;
  std::vector<Rect> currentDirtyRects;
  std::condition_variable frameCv;
  uint64_t frameCounter = 0;

  // DXGI
#ifdef _WIN32
  ID3D11Device *d3dDevice = nullptr;
  ID3D11DeviceContext *d3dContext = nullptr;
  IDXGIOutputDuplication *dxgiOutputDuplication = nullptr;
  DXGI_OUTPUT_DESC outputDesc;
  ID3D11Texture2D *stagingTexture = nullptr;
#endif
};

Napi::FunctionReference VncServer::constructor;

// --- Implementation ---

Napi::Object VncServer::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);
  Napi::Function func = DefineClass(
      env, "VncServer",
      {
          InstanceMethod("start", &VncServer::Start),
          InstanceMethod("stop", &VncServer::Stop),
          InstanceMethod("setQuality", &VncServer::SetQuality),
          InstanceMethod("getActiveClientsCount",
                         &VncServer::GetActiveClientsCount),
          InstanceMethod("onClientConnected", &VncServer::OnClientConnected),
          InstanceMethod("onClientDisconnected",
                         &VncServer::OnClientDisconnected),
          InstanceMethod("onError", &VncServer::OnError),
      });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("VncServer", func);
  return exports;
}

VncServer::VncServer(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<VncServer>(info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "Options expected").ThrowAsJavaScriptException();
    return;
  }
  Napi::Object options = info[0].As<Napi::Object>();
  this->port = options.Has("port")
                   ? options.Get("port").As<Napi::Number>().Int32Value()
                   : 5900;
  this->password = options.Has("password")
                       ? options.Get("password").As<Napi::String>().Utf8Value()
                       : "";

  this->running = false;
  this->captureRunning = false;
  this->activeClients = 0;

  // Pre-allocate framebuffer (default 1920x1080)
  this->serverFramebuffer.resize(1920 * 1080 * 4);
}

VncServer::~VncServer() {
  Stop(Napi::CallbackInfo(this->Env(), Napi::CallbackInfo::New(this->Env())));
  if (onConnectTsfn)
    onConnectTsfn.Release();
  if (onDisconnectTsfn)
    onDisconnectTsfn.Release();
  if (onErrorTsfn)
    onErrorTsfn.Release();
#ifdef _WIN32
  CleanupDXGI();
#endif
}

// ... Event Methods (Same as before) ...
Napi::Value VncServer::OnClientConnected(const Napi::CallbackInfo &info) {
  this->onConnectTsfn = Napi::ThreadSafeFunction::New(
      info.Env(), info[0].As<Napi::Function>(), "OnConnect", 0, 1);
  return info.Env().Null();
}
Napi::Value VncServer::OnClientDisconnected(const Napi::CallbackInfo &info) {
  this->onDisconnectTsfn = Napi::ThreadSafeFunction::New(
      info.Env(), info[0].As<Napi::Function>(), "OnDisconnect", 0, 1);
  return info.Env().Null();
}
Napi::Value VncServer::OnError(const Napi::CallbackInfo &info) {
  this->onErrorTsfn = Napi::ThreadSafeFunction::New(
      info.Env(), info[0].As<Napi::Function>(), "OnError", 0, 1);
  return info.Env().Null();
}

Napi::Value VncServer::Start(const Napi::CallbackInfo &info) {
  if (this->running)
    return info.Env().Null();
  this->running = true;
  this->networkThread = std::thread(&VncServer::NetworkLoop, this);
  return info.Env().Null();
}

Napi::Value VncServer::Stop(const Napi::CallbackInfo &info) {
  this->running = false;
  this->captureRunning = false;
  if (this->networkThread.joinable())
    this->networkThread.join();
  if (this->captureThread.joinable())
    this->captureThread.join();
  return info.Env().Null();
}

Napi::Value VncServer::SetQuality(const Napi::CallbackInfo &info) {
  return info.Env().Null();
}
Napi::Value VncServer::GetActiveClientsCount(const Napi::CallbackInfo &info) {
  return Napi::Number::New(info.Env(), this->activeClients);
}

// --- Network Logic ---

void VncServer::NetworkLoop() {
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
  SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in service;
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = INADDR_ANY;
  service.sin_port = htons(this->port);
  bind(serverSocket, (SOCKADDR *)&service, sizeof(service));
  listen(serverSocket, SOMAXCONN);

  while (this->running) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(serverSocket, &readfds);
    timeval timeout = {1, 0};
    if (select(0, &readfds, NULL, NULL, &timeout) > 0) {
      SOCKET clientSocket = accept(serverSocket, NULL, NULL);
      if (clientSocket != INVALID_SOCKET) {
        std::thread(&VncServer::ClientHandler, this, (uintptr_t)clientSocket,
                    "client")
            .detach();
      }
    }
  }
  closesocket(serverSocket);
  WSACleanup();
#endif
}

void VncServer::ClientHandler(uintptr_t socketPtr, std::string id) {
#ifdef _WIN32
  SOCKET clientSocket = (SOCKET)socketPtr;
  this->activeClients++;

  // 1. WebSocket Handshake
  if (!HandshakeWebSocket(clientSocket)) {
    closesocket(clientSocket);
    this->activeClients--;
    return;
  }

  // 2. Start Capture if needed
  if (!this->captureRunning) {
    this->captureRunning = true;
    this->captureThread = std::thread(&VncServer::CaptureLoop, this);
  }

  // 3. RFB Handshake
  if (!HandshakeRFB(clientSocket, this->width, this->height, "NodeVNC")) {
    closesocket(clientSocket);
    this->activeClients--;
    return;
  }

  // Notify JS
  if (this->onConnectTsfn) {
    auto cb = [](Napi::Env env, Napi::Function jsCb) {
      jsCb.Call({Napi::Object::New(env)});
    };
    this->onConnectTsfn.BlockingCall(cb);
  }

  // 4. Main Loop
  uint64_t lastFrameSeen = 0;
  bool updateRequested = true; // Start true to send initial frame

  while (this->running) {
    // Check for incoming data (RFB messages)
    unsigned long bytesAvailable = 0;
    ioctlsocket(clientSocket, FIONREAD, &bytesAvailable);

    if (bytesAvailable > 0) {
      // Read RFB message type
      uint8_t msgType;
      int n = recv(clientSocket, (char *)&msgType, 1, 0);
      if (n <= 0)
        break; // Client disconnected

      switch (msgType) {
      case 0: // SetPixelFormat
      {
        char buf[19];
        recv(clientSocket, buf, 19, 0);
      } break;
      case 2: // SetEncodings
      {
        char buf[3];
        recv(clientSocket, buf, 3, 0);
        uint16_t numEncodings = (buf[1] << 8) | buf[2];
        char *encBuf = new char[numEncodings * 4];
        recv(clientSocket, encBuf, numEncodings * 4, 0);
        delete[] encBuf;
      } break;
      case 3: // FramebufferUpdateRequest
      {
        char buf[9];
        recv(clientSocket, buf, 9, 0);
        updateRequested = true; // Client requests update
      } break;
      case 4: // KeyEvent
      {
        char buf[7];
        recv(clientSocket, buf, 7, 0);
        // TODO: Parse and call SendInput
      } break;
      case 5: // PointerEvent
      {
        char buf[5];
        recv(clientSocket, buf, 5, 0);
        // TODO: Parse and call SendInput
      } break;
      default:
        // Unknown message, drain buffer
        char buf[1024];
        recv(clientSocket, buf, sizeof(buf), 0);
        break;
      }
    }

    // Check for new frame AND client requested update
    std::unique_lock<std::mutex> lock(this->framebufferMutex);
    if (updateRequested && this->frameCounter > lastFrameSeen) {
      // Send update
      SendFrameUpdate(clientSocket, this->currentDirtyRects,
                      this->serverFramebuffer, this->width, this->height);
      lastFrameSeen = this->frameCounter;
      updateRequested = false; // Reset until next request
    }
    lock.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  closesocket(clientSocket);
  this->activeClients--;
#endif
}

#ifdef _WIN32
// Minimal WebSocket Handshake (Assumes polite client)
bool VncServer::HandshakeWebSocket(SOCKET s) {
  char buf[4096];
  int n = recv(s, buf, sizeof(buf) - 1, 0);
  if (n <= 0)
    return false;
  buf[n] = 0;
  std::string req(buf);

  // Find Sec-WebSocket-Key
  std::string keyHeader = "Sec-WebSocket-Key: ";
  size_t pos = req.find(keyHeader);
  if (pos == std::string::npos)
    return false; // Not a websocket request?

  size_t end = req.find("\r\n", pos);
  std::string key =
      req.substr(pos + keyHeader.length(), end - (pos + keyHeader.length()));

  // Compute SHA1 + Base64 for WebSocket Accept key
  std::string acceptKey = ComputeSHA1Base64(key);

  std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: " +
                     acceptKey + "\r\n\r\n";
  send(s, resp.c_str(), resp.length(), 0);
  return true;
}

bool VncServer::HandshakeRFB(SOCKET s, int w, int h, std::string name) {
  // 1. ProtocolVersion
  const char *ver = "RFB 003.008\n";
  send(s, ver, 12, 0);
  char buf[12];
  recv(s, buf, 12, 0); // Client version

  // 2. Security (1 = None)
  char sec[] = {1, 1}; // Count 1, Type 1 (None)
  send(s, sec, 2, 0);
  recv(s, buf, 1, 0); // Client Init (Shared flag)

  // 3. Server Init
  // Width (2), Height (2), PixelFormat (16), NameLen (4), Name
  std::vector<uint8_t> initMsg;
  initMsg.resize(24 + name.length());

  // W/H
  initMsg[0] = (w >> 8) & 0xFF;
  initMsg[1] = w & 0xFF;
  initMsg[2] = (h >> 8) & 0xFF;
  initMsg[3] = h & 0xFF;

  // Pixel Format (32-bit TrueColor RGBA)
  initMsg[4] = 32; // BPP
  initMsg[5] = 24; // Depth
  initMsg[6] = 0;  // BigEndian
  initMsg[7] = 1;  // TrueColor
  initMsg[8] = 0;
  initMsg[9] = 255; // RedMax
  initMsg[10] = 0;
  initMsg[11] = 255; // GreenMax
  initMsg[12] = 0;
  initMsg[13] = 255; // BlueMax
  initMsg[14] = 16;  // RedShift
  initMsg[15] = 8;   // GreenShift
  initMsg[16] = 0;   // BlueShift

  // Name
  uint32_t nameLen = name.length();
  initMsg[20] = (nameLen >> 24) & 0xFF;
  initMsg[21] = (nameLen >> 16) & 0xFF;
  initMsg[22] = (nameLen >> 8) & 0xFF;
  initMsg[23] = nameLen & 0xFF;
  memcpy(&initMsg[24], name.c_str(), nameLen);

  send(s, (char *)initMsg.data(), initMsg.size(), 0);
  return true;
}

void VncServer::SendFrameUpdate(SOCKET s, const std::vector<Rect> &rects,
                                const std::vector<uint8_t> &fb, int fbW,
                                int fbH) {
  // Msg Type 0 (FramebufferUpdate)
  // Padding (1)
  // Number of Rects (2)
  if (rects.empty())
    return;

  std::vector<uint8_t> header(4);
  header[0] = 0;
  header[1] = 0;
  uint16_t count = rects.size();
  header[2] = (count >> 8) & 0xFF;
  header[3] = count & 0xFF;
  send(s, (char *)header.data(), 4, 0);

  for (const auto &r : rects) {
    // Rect Header (12 bytes)
    // X, Y, W, H, Encoding (0 = Raw)
    uint8_t rh[12];
    rh[0] = (r.x >> 8) & 0xFF;
    rh[1] = r.x & 0xFF;
    rh[2] = (r.y >> 8) & 0xFF;
    rh[3] = r.y & 0xFF;
    rh[4] = (r.w >> 8) & 0xFF;
    rh[5] = r.w & 0xFF;
    rh[6] = (r.h >> 8) & 0xFF;
    rh[7] = r.h & 0xFF;
    rh[8] = 0;
    rh[9] = 0;
    rh[10] = 0;
    rh[11] = 0; // Raw Encoding
    send(s, (char *)rh, 12, 0);

    // Pixel Data (Raw)
    // Send line by line
    for (int y = 0; y < r.h; y++) {
      int srcIdx = ((r.y + y) * fbW + r.x) * 4;
      send(s, (char *)&fb[srcIdx], r.w * 4, 0);
    }
  }
}
#endif

// --- Capture Logic ---

void VncServer::CaptureLoop() {
#ifdef _WIN32
  InitializeDXGI();

  while (this->running && this->captureRunning) {
    if (this->activeClients == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    std::vector<uint8_t> frameBuffer; // Not used, we use serverFramebuffer
    int frameW, frameH;
    std::vector<Rect> dirtyRects;

    if (AcquireFrame(frameBuffer, frameW, frameH, dirtyRects)) {
      // Frame Acquired!
      std::lock_guard<std::mutex> lock(this->framebufferMutex);

      // Update Dirty Rects
      this->currentDirtyRects = dirtyRects;

      // If full update needed (e.g. first frame), add full rect
      if (dirtyRects.empty()) {
        this->currentDirtyRects.push_back({0, 0, this->width, this->height});
      }

      this->frameCounter++;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }
  CleanupDXGI();
#endif
}

#ifdef _WIN32
void VncServer::InitializeDXGI() {
  // ... (Same as previous, ensure d3dDevice etc are init) ...
  // For brevity, assuming previous implementation or standard DXGI init
  D3D11_CREATE_DEVICE_FLAG flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL featureLevel;
  D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                    featureLevels, 1, D3D11_SDK_VERSION, &d3dDevice,
                    &featureLevel, &d3dContext);

  IDXGIDevice *dxgiDevice = nullptr;
  d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgiDevice);
  IDXGIAdapter *dxgiAdapter = nullptr;
  dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&dxgiAdapter);
  dxgiDevice->Release();
  IDXGIOutput *dxgiOutput = nullptr;
  dxgiAdapter->EnumOutputs(0, &dxgiOutput);
  dxgiAdapter->Release();
  IDXGIOutput1 *dxgiOutput1 = nullptr;
  dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void **)&dxgiOutput1);
  dxgiOutput->Release();
  dxgiOutput1->DuplicateOutput(d3dDevice, &dxgiOutputDuplication);
  dxgiOutput1->Release();
  dxgiOutputDuplication->GetDesc(&outputDesc);

  // Update dimensions from DXGI
  this->width = outputDesc.ModeDesc.Width;
  this->height = outputDesc.ModeDesc.Height;
  this->serverFramebuffer.resize(this->width * this->height * 4);
}
}

void VncServer::CleanupDXGI() {
  if (stagingTexture) {
    stagingTexture->Release();
    stagingTexture = nullptr;
  }
  if (dxgiOutputDuplication) {
    dxgiOutputDuplication->Release();
    dxgiOutputDuplication = nullptr;
  }
  if (d3dContext) {
    d3dContext->Release();
    d3dContext = nullptr;
  }
  if (d3dDevice) {
    d3dDevice->Release();
    d3dDevice = nullptr;
  }
}

bool VncServer::AcquireFrame(std::vector<uint8_t> &buffer, int &width,
                             int &height, std::vector<Rect> &dirtyRects) {
  if (!dxgiOutputDuplication)
    return false;

  DXGI_OUTDUPL_FRAME_INFO frameInfo;
  IDXGIResource *desktopResource = nullptr;
  HRESULT hr = dxgiOutputDuplication->AcquireNextFrame(100, &frameInfo,
                                                       &desktopResource);
  if (FAILED(hr))
    return false;

  // Get Dirty Rects from DXGI metadata
  if (frameInfo.TotalMetadataBufferSize > 0) {
    UINT bufSize = frameInfo.TotalMetadataBufferSize;
    std::vector<BYTE> metaBuf(bufSize);
    UINT moveCount = 0;

    hr = dxgiOutputDuplication->GetFrameMoveRects(
        bufSize, (DXGI_OUTDUPL_MOVE_RECT *)metaBuf.data(), &moveCount);

    // Get Dirty Rects
    UINT dirtyCount = 0;
    hr = dxgiOutputDuplication->GetFrameDirtyRects(
        bufSize, (RECT *)metaBuf.data(), &dirtyCount);

    if (SUCCEEDED(hr) && dirtyCount > 0) {
      RECT *rects = (RECT *)metaBuf.data();
      for (UINT i = 0; i < dirtyCount; i++) {
        dirtyRects.push_back({(int)rects[i].left, (int)rects[i].top,
                              (int)(rects[i].right - rects[i].left),
                              (int)(rects[i].bottom - rects[i].top)});
      }
    }
  }

  // If no dirty rects, assume full update (e.g., first frame or accumulation)
  if (dirtyRects.empty()) {
    dirtyRects.push_back({0, 0, this->width, this->height});
  }

  ID3D11Texture2D *desktopImage = nullptr;
  desktopResource->QueryInterface(__uuidof(ID3D11Texture2D),
                                  (void **)&desktopImage);
  desktopResource->Release();

  if (!stagingTexture) {
    D3D11_TEXTURE2D_DESC desc;
    desktopImage->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture);
  }

  d3dContext->CopyResource(stagingTexture, desktopImage);
  desktopImage->Release();

  D3D11_MAPPED_SUBRESOURCE map;
  if (SUCCEEDED(d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &map))) {
    // Copy to serverFramebuffer
    // Convert BGRA -> RGBA
    uint8_t *src = (uint8_t *)map.pData;
    uint8_t *dst = this->serverFramebuffer.data();
    int w = this->width;
    int h = this->height;

    for (int i = 0; i < w * h; i++) {
      dst[i * 4 + 0] = src[i * 4 + 2]; // R
      dst[i * 4 + 1] = src[i * 4 + 1]; // G
      dst[i * 4 + 2] = src[i * 4 + 0]; // B
      dst[i * 4 + 3] = 255;            // A
    }

    d3dContext->Unmap(stagingTexture, 0);
  }

  dxgiOutputDuplication->ReleaseFrame();
  return true;
}

void VncServer::SendInput(int type, int x, int y, int button, int key) {
  INPUT input = {0};
  if (type == 0) { // Mouse
    input.type = INPUT_MOUSE;
    input.mi.dx = x * (65535 / this->width);
    input.mi.dy = y * (65535 / this->height);
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    if (button == 1)
      input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
    else if (button == 2)
      input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
  } else { // Keyboard
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
  }
  ::SendInput(1, &input, sizeof(INPUT));
}
#endif

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  return VncServer::Init(env, exports);
}
NODE_API_MODULE(vnc_server, Init)