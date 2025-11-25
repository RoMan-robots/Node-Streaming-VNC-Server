{
  "targets": [
    {
      "target_name": "vnc_server",
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "sources": [
        "native/vnc_server.cc"
      ],
      "include_dirs": [
        "node_modules/node-addon-api"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "conditions": [
        ['OS=="win"', {
          "libraries": [ 
            "-ld3d11.lib", 
            "-ldxgi.lib", 
            "-luser32.lib" 
          ],
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1
            }
          }
        }]
      ]
    }
  ]
}
