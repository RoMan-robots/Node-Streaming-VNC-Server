import { EventEmitter } from 'events';
import { VncServerOptions, QualityOptions, ClientInfo } from './types';
const addon = require('bindings')('vnc_server');

export class VncServer extends EventEmitter {
    private _nativeServer: any;
    private _options: VncServerOptions;

    constructor(options: VncServerOptions) {
        super();
        this._options = options;
        this._nativeServer = new addon.VncServer(options);

        // Setup native event listeners
        this._nativeServer.onClientConnected((client: ClientInfo) => {
            this.emit('client-connected', client);
        });

        this._nativeServer.onClientDisconnected((client: ClientInfo) => {
            this.emit('client-disconnected', client);
        });

        this._nativeServer.onError((error: Error) => {
            this.emit('error', error);
        });
    }

    start(): void {
        try {
            this._nativeServer.start();
        } catch (error) {
            throw error;
        }
    }

    stop(): void {
        try {
            this._nativeServer.stop();
        } catch (error) {
            throw error;
        }
    }

    setQuality(options: QualityOptions): void {
        this._nativeServer.setQuality(options);
    }

    getActiveClientsCount(): number {
        return this._nativeServer.getActiveClientsCount();
    }
}
