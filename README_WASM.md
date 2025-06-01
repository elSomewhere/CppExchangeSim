# Exchange Simulation - WebAssembly Build

This directory contains a WebAssembly version of the C++ exchange simulation that can run in web browsers.

## 🔧 Prerequisites

### 1. Install Emscripten SDK

Download and install the Emscripten SDK:

```bash
# Clone the repository
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install the latest version
./emsdk install latest
./emsdk activate latest

# Activate the environment (add to your shell profile for persistence)
source ./emsdk_env.sh
```

Verify installation:
```bash
emcc --version
```

### 2. System Requirements

- **C++17 compatible compiler** (usually comes with Emscripten)
- **Python 3** (for development server)
- **Modern web browser** supporting WebAssembly

## 🏗️ Building

### Basic Build

```bash
# Build the WebAssembly module
make

# This creates:
# - exchange_simulation.js (JavaScript glue code)
# - exchange_simulation.wasm (WebAssembly binary)
```

### Debug Build

```bash
# Build with debug symbols and extra checks
make debug
```

### Clean

```bash
# Remove build artifacts
make clean
```

## 🚀 Running

### Option 1: Using VS Code Live Server

1. Open the project in VS Code
2. Install the "Live Server" extension
3. Right-click on `index.html` and select "Open with Live Server"
4. Navigate to the opened URL in your browser

### Option 2: Using Python Development Server

```bash
# Start the server (builds if necessary)
make serve

# Then open http://localhost:8000 in your browser
```

### Option 3: Using Any HTTP Server

You can use any HTTP server that serves static files. For example:

```bash
# Node.js
npx http-server

# Python
python3 -m http.server 8000

# PHP
php -S localhost:8000
```

**⚠️ Important**: You cannot run the WebAssembly module by opening `index.html` directly in the browser due to CORS restrictions. You must serve it through an HTTP server.

## 🎮 Usage

### Web Interface

The web interface provides a comprehensive control panel for configuring simulation parameters:

#### Simulation Parameters
- **Number of Agents**: 10-1000 trading agents
- **Trading Symbol**: Symbol name (e.g., BTC/USD)
- **Random Seed**: For reproducible results
- **Speed Factor**: Simulation speed multiplier

#### Order Timeout Settings
- **Distribution**: Log-normal, Pareto, or Mixed
- **Median**: Median timeout in seconds
- **Sigma**: Distribution parameter

#### Order Size Configuration
- **Min/Max Size Ranges**: Control order size distributions

### Operation Flow

1. **Configure Parameters**: Adjust settings in the left panel
2. **Initialize**: Click "Initialize" to set up the simulation
3. **Start**: Click "Start" to begin the simulation
4. **Monitor**: Watch real-time order book updates
5. **Stop**: Click "Stop" to halt the simulation (optional)

### Real-time Data

The right panel displays:
- **Live Order Book**: Top 10 bid/ask levels
- **Status Information**: Simulation state, queue size, update count
- **Visual Indicators**: Color-coded bids (green) and asks (red)

## 📊 Output Data

### L2 Order Book Events

Each order book update contains:

```javascript
{
    symbol: "BTC/USD",
    exchange_ts: 1234567890,  // Exchange timestamp (ms)
    ingress_ts: 1234567891,   // Ingress timestamp (ms)
    bids: [
        { price: 50000.00, quantity: 1.2345 },
        // ... up to 10 levels
    ],
    asks: [
        { price: 50010.00, quantity: 0.8765 },
        // ... up to 10 levels
    ]
}
```

### JavaScript Integration

To integrate the simulation into your own JavaScript application:

```javascript
// Load the module
ExchangeSimulationModule().then(Module => {
    // Create simulation instance
    const sim = new Module.ExchangeSimulation();
    
    // Configure parameters
    sim.setAgents(100);
    sim.setSymbol("BTC/USD");
    sim.setSeed(42);
    
    // Set L2 callback
    sim.setL2Callback((event) => {
        console.log('Order book update:', event);
        // Process the order book data
    });
    
    // Initialize and start
    if (sim.initialize()) {
        sim.start();
    }
    
    // Cleanup when done
    sim.cleanup();
    sim.delete();
});
```

## 🔧 Customization

### Modifying Parameters

To add new parameters:

1. Add the parameter to `SimulationParams` struct in `wasm_main.cpp`
2. Add a setter method to `ExchangeSimulation` class
3. Expose the method in the Emscripten bindings
4. Add UI controls in `index.html`

### Extending Callbacks

To add more event callbacks:

1. Modify `L2WasmHook` to handle additional event types
2. Add corresponding JavaScript callback setters
3. Update the UI to display the new data

## 🐛 Troubleshooting

### Common Issues

**"Module not found" error**
- Ensure the WebAssembly files are served via HTTP, not file://
- Check that `exchange_simulation.js` is accessible

**Compilation errors**
- Verify Emscripten is properly installed and activated
- Check that all header files in `src/` are accessible
- Ensure C++17 support is available

**Runtime crashes**
- Try the debug build: `make debug`
- Check browser console for detailed error messages
- Verify parameter ranges are valid

**Performance issues**
- Reduce number of agents
- Increase speed factor
- Consider using Firefox or Chrome for better WebAssembly performance

### Browser Compatibility

Tested on:
- ✅ Chrome 90+
- ✅ Firefox 85+
- ✅ Safari 14+
- ✅ Edge 90+

### Memory Usage

The simulation uses approximately:
- **Base**: 32MB initial memory
- **Per Agent**: ~1KB additional memory
- **Peak**: May grow to 100MB+ with 1000 agents

## 📁 File Structure

```
├── src/                     # Core C++ simulation framework (unchanged)
├── wasm_main.cpp           # WebAssembly main entry point
├── L2WasmHook.h           # WebAssembly L2 event hook
├── ZeroIntelligenceMarketMaker.h  # Trading agent implementation
├── Makefile               # Build configuration
├── index.html             # Web interface
├── README_WASM.md         # This file
└── exchange_simulation.*  # Generated WebAssembly files
```

## 🤝 Contributing

When modifying the WebAssembly version:

1. Keep the `src/` directory unchanged (core framework)
2. Test with both regular and debug builds
3. Verify browser compatibility
4. Update this README if adding new features

## 📜 License

Same license as the main project. 