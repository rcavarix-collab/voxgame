// Native-test stand-in for <d3d11.h>: world.cpp only ever Release()s
// chunk buffers, and tests never create any.
#pragma once
struct ID3D11Buffer { void Release() {} };
