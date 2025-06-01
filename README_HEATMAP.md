# Real-Time Orderbook Heatmap Visualization

This document describes the implementation of a professional-grade real-time orderbook heatmap visualization system for the PyCppExchangeSim trading simulation.

## Overview

The heatmap system provides a **classical orderbook depth visualization** as commonly seen on professional trading platforms and research tools. It renders Level-2 (L2) market data as a time-series heatmap where:

- **X-axis** = Time (discrete snapshots)
- **Y-axis** = Price levels (discretized around mid-price)  
- **Color intensity** = Volume depth at each price level
- **Visual style** = Ikeda-inspired stark black/white aesthetic

## Architecture

### Core Components

1. **`HeatmapBuffer.h`** - Efficient C++ data buffer system
   - `HeatmapSnapshot`: Single timestamp L2 data representation
   - `PriceGrid`: Discretized price level management with auto-recentering
   - `HeatmapMatrix`: Time-series matrix for volume aggregation
   - `HeatmapBuffer`: Main buffer class with configurable parameters

2. **`L2HeatmapHook.h`** - Enhanced L2 data hook
   - Extends functionality of `L2PrinterHook` and `L2WasmHook`
   - Real-time data buffering and WebAssembly integration
   - Configurable update frequency and data transmission

3. **`heatmap.html`** - Standalone visualization interface
   - Canvas-based rendering with Ikeda-style aesthetics
   - Real-time data reception and display
   - Interactive controls and configuration

4. **`index.html`** (Enhanced) - Main simulation interface
   - Integrated heatmap controls and configuration
   - Window management and data relay

### Data Flow

```
C++ Simulation → L2HeatmapHook → WebAssembly → JavaScript → Heatmap Canvas
     ↓
 HeatmapBuffer (time-series storage)
     ↓
 Aggregated volume data → Visualization
```

## Features

### Core Functionality
- **Real-time L2 orderbook visualization** with time-series heatmap display
- **Level-2 market data integration** from WebAssembly trading simulation
- **Time-series visualization**: X-axis=time, Y-axis=price levels, color=volume depth
- **Historical data scrolling**: Older orderbook states scroll left as new data arrives
- **Professional trading UI** with Ikeda-style stark black/white aesthetics
- **Interactive controls** with keyboard shortcuts and configuration panel

### Time-Series Heatmap Implementation

The heatmap now displays **proper time-series behavior**:

1. **Each column represents a different time point** - not just the current snapshot
2. **Historical data buffer** maintains up to 300 snapshots of orderbook states
3. **Scrolling visualization** - older data moves left as new data arrives on the right
4. **Real-time progression** - you can see how orderbook depth changes over time
5. **Volume normalization** across all visible time points using 95th percentile scaling

#### Key Components:

- **`addToDataBuffer()`**: Maintains historical snapshots with automatic buffer management
- **`renderTimeSeriesHeatmap()`**: Renders all historical columns from left (oldest) to right (newest)
- **`renderColumn()`**: Renders individual time point as bid/ask volume intensities
- **`calculateGlobalVolumeStats()`**: Normalizes colors across all visible time points
- **`drawTimeAxis()`**: Shows time progression with "NOW" marker on rightmost column

### Visual Improvements

- **Time axis labels**: Shows relative time from oldest visible data (T-10, T-5, T-0=NOW)
- **Current time indicator**: White vertical line marks the most recent data column
- **Consistent color scaling**: Volume intensities normalized across entire visible history
- **Smooth scrolling**: New data appears on right, historical data shifts left seamlessly

### Data Management
- **Configurable buffer size**: 50-1000 historical snapshots
- **Smart price grid**: Auto-recentering around mid-price drift
- **Efficient aggregation**: Volume accumulation by price level
- **Outlier-resistant scaling**: 95th percentile normalization

### Visualization
- **Professional aesthetics**: Stark black/white Ikeda-style interface
- **Real-time updates**: Efficient incremental rendering
- **Interactive controls**: Play/pause, configuration, fullscreen
- **Responsive design**: Auto-sizing and responsive layout

### Configuration Options
- **Buffer Size**: Number of historical snapshots (50-1000)
- **Price Levels**: Number of price levels around mid-price (50-500)
- **Tick Size**: Price increment per level ($0.1-$10.0)
- **Update Frequency**: Heatmap updates per N L2 updates (1-50)
- **Visual Intensity**: Color scaling adjustment (1-10)

## Testing the Time-Series Heatmap

To verify the new time-series scrolling behavior:

### 1. **Standalone Heatmap Test** 
```bash
# Start local server
python3 -m http.server 8000

# Open http://localhost:8000/heatmap.html
```

**Expected behavior:**
- Initially see a black canvas
- After few seconds, columns start appearing from left to right
- Each new data point adds a column on the right side
- Older columns gradually shift left and disappear when buffer is full
- Mid-price line shows current price level across all time columns
- Time axis shows "T-N" labels with "NOW" marker on rightmost column

### 2. **WebAssembly Integration Test**
```bash
# Compile and run
make && python3 -m http.server 8000

# Open http://localhost:8000/index.html
# Click "Open Heatmap Window"
```

**Expected behavior:**
- Real orderbook data from trading simulation
- Each L2 update creates new column on right
- Historical depth patterns visible scrolling left
- Volume spikes and market movements clearly visible over time
- Buffer usage indicator shows current vs maximum snapshots

### 3. **Visual Verification Points**

**Time Progression:**
- ✅ New data appears on RIGHT side
- ✅ Older data scrolls to LEFT and disappears
- ✅ Each column represents different time point
- ✅ "NOW" marker tracks the newest column

**Volume Visualization:**
- ✅ Bid volumes on left half of each cell (darker gray to white)
- ✅ Ask volumes on right half of each cell (darker gray to white)
- ✅ Volume intensity consistent across all visible time points
- ✅ Mid-price line shows market center over time

**Data Management:**
- ✅ Buffer usage increases until max size reached
- ✅ Oldest data automatically removed when buffer full
- ✅ Smooth scrolling without visual artifacts
- ✅ Configurable buffer size affects visible history

### 4. **Performance Indicators**

- **Smooth rendering** at 100ms update intervals
- **No memory leaks** with continuous operation
- **Responsive controls** during active data streaming
- **Stable frame rate** with full buffer (300 snapshots)

## Usage

### 1. Opening the Heatmap

From the main simulation interface:
1. Navigate to the **🔥 Heatmap Visualization** section
2. Click **🔥 Open Heatmap** to launch the visualization window
3. Configure parameters using **⚙️ Config** if needed

### 2. Keyboard Shortcuts (in heatmap window)

- **Spacebar**: Toggle play/pause
- **R**: Reset visualization
- **C**: Toggle configuration panel
- **F**: Toggle fullscreen mode

### 3. Configuration

#### Main Interface
- **Buffer Size**: Adjust memory usage vs. history length
- **Price Levels**: Balance detail vs. performance
- **Tick Size**: Match your market's tick increments
- **Update Frequency**: Reduce for better performance

#### Heatmap Window
- **Intensity Slider**: Adjust color contrast
- Real-time status monitoring
- Buffer usage indicators

## Technical Implementation

### C++ Backend

#### HeatmapBuffer Class
```cpp
// Create buffer with 300 snapshots, 200 price levels, $1 tick size
HeatmapBuffer buffer(300, 200, 1.0);

// Add L2 data
buffer.add_l2_snapshot(l2_event);

// Get visualization data
auto viz_data = buffer.get_visualization_data();
```

#### L2HeatmapHook Integration
```cpp
// Configure and register hook
auto hook = std::make_unique<L2HeatmapHook>(
    buffer_size, price_levels, tick_size,
    enable_console, enable_l2, enable_heatmap, update_freq
);

// Register with event bus
sim.get_event_bus().register_pre_publish_hook(hook.get());
```

### JavaScript Frontend

#### WebAssembly Integration
```javascript
// Set up callbacks
simulation.setL2Callback(onL2Update);
simulation.setHeatmapCallback(onHeatmapUpdate);

// Configure parameters
simulation.setHeatmapBufferSize(300);
simulation.setHeatmapPriceLevels(200);
simulation.setHeatmapTickSize(1.0);
```

#### Rendering Pipeline
```javascript
// Receive heatmap data
function onHeatmapUpdate(heatmapData) {
    // Extract volume arrays and metadata
    const { bidVolumes, askVolumes, stats } = heatmapData;
    
    // Normalize using 95th percentile
    const maxVolume = Math.max(stats.p95BidVolume, stats.p95AskVolume);
    
    // Render with Ikeda-style color mapping
    renderHeatmap(heatmapData);
}
```

## Performance Considerations

### Memory Usage
- **Buffer size** directly affects memory consumption
- Typical usage: 300 snapshots × 200 levels = ~240KB per session
- Automatic cleanup of old data when buffer is full

### Update Frequency
- **High frequency** (every update): Real-time, higher CPU usage
- **Medium frequency** (every 5 updates): Good balance
- **Low frequency** (every 10+ updates): Reduced load, less granular

### Rendering Optimization
- Canvas-based rendering for performance
- Efficient color mapping with minimal calculations
- Incremental updates (only newest data)

## Customization

### Color Schemes
Modify `getIntensityColor()` in `heatmap.html`:
```javascript
getIntensityColor(intensity, side) {
    // Current: Ikeda-style stark B&W
    if (intensity < 0.1) return '#000000';
    else if (intensity < 0.3) return '#1a1a1a';
    // ... customize color mapping
}
```

### Price Grid Behavior
Adjust recentering in `HeatmapBuffer.h`:
```cpp
// Current: Recenter every 50 snapshots if drift > 1/8 of range
static constexpr size_t GRID_UPDATE_FREQUENCY = 50;
```

## Troubleshooting

### Common Issues

1. **Heatmap window not opening**
   - Check popup blocker settings
   - Ensure `heatmap.html` is in the same directory

2. **No data in visualization**
   - Verify simulation is running and generating L2 data
   - Check JavaScript console for WebAssembly errors
   - Ensure heatmap updates are enabled

3. **Performance issues**
   - Reduce buffer size or price levels
   - Increase update frequency interval
   - Lower visual intensity setting

### Debug Information

Monitor browser console for:
```
[Heatmap] L2 update received: {...}
[Heatmap] Heatmap data received: {...}
[JavaScript] Heatmap callback called!
```

## Integration Notes

### WebAssembly Build
Ensure Emscripten bindings are included:
```cpp
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(l2_heatmap_hook) {
    emscripten::class_<L2HeatmapHook>("L2HeatmapHook")
        .function("setL2Callback", &L2HeatmapHook::set_l2_callback)
        .function("setHeatmapCallback", &L2HeatmapHook::set_heatmap_callback)
        // ... other bindings
}
#endif
```

### C++ Compilation
Include new headers in `main.cpp`:
```cpp
#include "HeatmapBuffer.h"
#include "L2HeatmapHook.h"
```

## Future Enhancements

### Potential Improvements
1. **Historical playback**: Scrub through recorded sessions
2. **Multiple timeframes**: Different aggregation periods
3. **Volume profile overlays**: Horizontal volume distribution
4. **Trade flow visualization**: Actual execution overlays
5. **Multi-symbol support**: Side-by-side comparisons
6. **Export functionality**: Save heatmap images/data

### Advanced Features
1. **Algorithmic pattern detection**: Identify iceberg orders, spoofing
2. **Market microstructure analysis**: Spread dynamics, depth changes
3. **Real exchange integration**: Live market data feeds
4. **Machine learning overlays**: Predicted price movements

## Conclusion

This heatmap system provides a professional-grade visualization tool that matches the quality and functionality found on commercial trading platforms. The modular architecture allows for easy customization and extension while maintaining high performance and visual appeal.

The implementation follows best practices for:
- **Real-time data processing**: Efficient buffering and aggregation
- **Professional UI/UX**: Clean, functional interface design  
- **Performance optimization**: Memory management and rendering efficiency
- **Extensibility**: Modular components for future enhancements

For technical support or feature requests, please refer to the main project documentation or create an issue in the project repository. 