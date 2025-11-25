export interface VncServerOptions {
    /**
     * The WebSocket port for noVNC clients.
     * Standard TCP VNC connections are not supported.
     */
    port: number;
    password?: string;
}


export interface QualityOptions {
    /**
     * JPEG (0-100) for TIGHT
     */
    jpegQuality?: number;
    /**
     * Compress level (0-9) for zlib
     */
    zlibLevel?: number;
}

export interface ClientInfo {
    id: string;
    address: string;
}
