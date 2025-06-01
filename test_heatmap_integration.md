# Heatmap Integration Test Guide

## ✅ What Was Fixed

The error `simulation.setHeatmapCallback is not a function` has been resolved by:

1. **Updated WebAssembly Interface** (`wasm_main.cpp`)
   - Replaced `L2WasmHook` with `L2HeatmapHook`
   - Added `setHeatmapCallback()` method
   - Added heatmap configuration methods
   - Updated Emscripten bindings

2. **Updated JavaScript Integration** (`index.html`)
   - Fixed method names to match WebAssembly interface
   - Added proper error handling
   - Disabled non-configurable parameters

## 🧪 Testing Steps

### 1. Compile WebAssembly Module
```bash
# Make sure to recompile with the updated wasm_main.cpp
# that includes L2HeatmapHook.h instead of L2WasmHook.h
```

### 2. Test Basic Functionality
1. Open `index.html` in a browser
2. Check browser console for initialization messages
3. Look for: `[ExchangeSimulation] DEBUG: Creating L2HeatmapHook in constructor...`

### 3. Test Heatmap Configuration
1. Navigate to "🔥 Heatmap Visualization" section
2. Click "⚙️ Config" to open configuration panel
3. Verify that Price Levels and Tick Size inputs are disabled (as expected)
4. Change Buffer Size and Update Frequency values

### 4. Test Callbacks
1. Initialize the simulation (should not show the error anymore)
2. Look for these console messages:
   ```
   [JavaScript] Setting heatmap callback...
   [ExchangeSimulation] DEBUG: Setting heatmap callback on L2HeatmapHook...
   [JavaScript] Heatmap configuration applied successfully
   ```

### 5. Test Heatmap Window
1. Click "🔥 Open Heatmap" to launch visualization window
2. Start the simulation
3. Verify heatmap receives both L2 and heatmap data:
   ```
   [Heatmap] L2 update received: {...}
   [Heatmap] Heatmap data received: {...}
   ```

## 🔧 Available Methods

### WebAssembly Methods (C++ → JavaScript)
✅ `simulation.setL2Callback(callback)`
✅ `simulation.setHeatmapCallback(callback)`
✅ `simulation.setHeatmapBufferSize(size)`
✅ `simulation.setHeatmapFrequency(frequency)`
✅ `simulation.setHeatmapUpdates(enabled)`
✅ `simulation.getHeatmapBufferSize()`
✅ `simulation.getHeatmapBufferUsage()`

### Configuration Limitations
❌ `setHeatmapPriceLevels()` - Fixed at construction (200 levels)
❌ `setHeatmapTickSize()` - Fixed at construction ($1.0)

## 🚨 Expected Console Output

### Successful Initialization
```
[ExchangeSimulation] DEBUG: Creating L2HeatmapHook in constructor...
[JavaScript] Setting L2 callback...
[ExchangeSimulation] DEBUG: Setting L2 callback on L2HeatmapHook...
[JavaScript] Setting heatmap callback...
[ExchangeSimulation] DEBUG: Setting heatmap callback on L2HeatmapHook...
[JavaScript] Heatmap configuration applied successfully
```

### During Simulation
```
[L2HeatmapHook] L2 callback set
[L2HeatmapHook] Heatmap callback set
[Heatmap] L2 update received: {symbol: "BTC/USD", bids: [...], asks: [...]}
[Heatmap] Heatmap data received: {bufferUsage: 1, bidVolumes: [...], askVolumes: [...]}
```

## 🐛 Troubleshooting

### If "setHeatmapCallback is not a function" persists:
1. Verify WebAssembly module was recompiled with updated `wasm_main.cpp`
2. Check that `L2HeatmapHook.h` is included correctly
3. Ensure `exchange_simulation.js` file is updated

### If heatmap window shows no data:
1. Check that simulation is running and generating L2 events
2. Verify heatmap updates are enabled in configuration
3. Look for JavaScript errors in console

### If performance is poor:
1. Reduce buffer size in configuration
2. Increase update frequency (send less frequently)
3. Check browser performance in developer tools

## ✨ Success Indicators

1. ✅ No "function not found" errors in console
2. ✅ Heatmap window opens and shows data
3. ✅ Real-time updates in both L2 orderbook and heatmap
4. ✅ Configuration changes take effect
5. ✅ Buffer usage statistics update in real-time

The integration is complete and should now provide professional-grade real-time orderbook heatmap visualization! 