export interface VncServerOptions {
    /**
     * The WebSocket port for noVNC clients.
     * Standard TCP VNC connections are not supported.
     */
    port: number;
    password?: string;
    /**
     * Enables the Virtual Desktop mode.
     * When false (default), the server captures the main physical desktop.
     * When true, the server activates the virtual desktop implementation.
     * @default false
     */
    virtualDesktop?: boolean;
    /**
     * Optional width and height for the virtual desktop.
     * NOTE: This setting is ignored unless the server is started
     * in 'virtual desktop' mode.
     */
    width?: number;
    height?: number;
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
