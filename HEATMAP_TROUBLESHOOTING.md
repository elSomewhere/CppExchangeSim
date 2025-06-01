# Heatmap Troubleshooting Guide

## Problem: L2 Events Not Displaying in Heatmap

Follow these steps to diagnose and fix heatmap display issues:

## Step 1: Quick Diagnosis

1. **Open your browser** and navigate to `http://localhost:8004`
2. **Open Developer Console** (F12 → Console tab)
3. **Copy and paste this diagnostic script:**

```javascript
// Copy the contents of debug_heatmap.js into the console
// Then run:
debugHeatmapSystem()
```

## Step 2: Apply Quick Fixes

If the diagnostic reveals issues, apply the automatic fixes:

```javascript
// Copy the contents of heatmap_fix.js into the console
// Then run:
fixHeatmapSystem()
```

## Step 3: Common Issues & Solutions

### Issue 1: "Heatmap is disabled in UI"
**Solution:** 
- Check the "Enable Heatmap" checkbox in the simulation settings
- Restart the simulation after enabling

### Issue 2: "Update frequency is high - might need many events"
**Solution:**
- Set **Heatmap Update Frequency** to `2` or `1` for faster updates
- Default `5` means heatmap updates every 5 L2 events

### Issue 3: "Buffer size is large - needs many snapshots"
**Solution:**
- Reduce **Heatmap Buffer Size** from `300` to `100` 
- Buffer must fill before first heatmap appears

### Issue 4: "No heatmap callbacks received"
**Potential causes:**
- Simulation not generating L2 events
- C++ callback not properly registered
- WASM binding issue

**Quick test:**
```javascript
testHeatmapFrequency() // Sets frequency to 1 for 30 seconds
```

## Step 4: Expected Event Timeline

For default settings (Buffer: 300, Frequency: 5):

1. **Events 1-300:** Buffer filling (no heatmap yet)
2. **Event 305:** First heatmap appears 
3. **Event 310:** Second heatmap update
4. **Event 315:** Third heatmap update
5. **Continues every 5 events**

To see heatmap faster, use:
- **Buffer Size:** 50-100 
- **Update Frequency:** 1-2

## Step 5: Alternative Testing

If main simulation has issues, try the standalone heatmap:

1. Open `http://localhost:8004/heatmap.html`
2. This uses synthetic data and should show a working heatmap immediately
3. If this works but main simulation doesn't, the issue is in the integration

## Step 6: Advanced Debugging

### Check Console Messages

Look for these patterns in the console:

**✅ Good signs:**
```
[EventScheduler] Heatmap callback triggered: data received
[EventScheduler] Heatmap data received at event X
[EventScheduler] Heatmap event processed in real-time
```

**❌ Problem signs:**
```
[EventScheduler] Heatmap callback triggered: no data
[EventScheduler] Events: 50, Callbacks: 0
[EventScheduler] Heatmap update error: ...
```

### Monitor Event Flow

```javascript
// Check if events are being processed
timingWorker.postMessage({type: 'GET_STATUS'});

// Check specific heatmap state
timingWorker.postMessage({type: 'HEATMAP_DEBUG'});
```

## Step 7: System Requirements

Ensure your system meets requirements:

- **Browser:** Chrome, Firefox, or Safari (latest versions)
- **WebAssembly:** Must be enabled (check `typeof WebAssembly`)
- **Memory:** At least 2GB available for large buffers
- **JavaScript:** Must be enabled

## Manual Configuration Fix

If automated fixes don't work, manually set these values:

```javascript
document.getElementById('heatmapEnabled').checked = true;
document.getElementById('heatmapBufferSize').value = '100';
document.getElementById('heatmapUpdateFreq').value = '2';
```

Then restart the simulation.

## Still Not Working?

1. **Check the README_HEATMAP_DEBUGGING.md** for more detailed technical information
2. **Try different browsers** - WebAssembly compatibility varies
3. **Check browser console for JavaScript errors** 
4. **Verify the .wasm file is loading correctly**
5. **Test with synthetic data** in `heatmap.html` to isolate the issue

## Expected Output

When working correctly, you should see:
- **Heatmap canvas** updating with colorful volume data
- **Console messages** confirming data reception
- **Buffer usage** counter increasing
- **Update count** incrementing

The heatmap shows:
- **Left side of cells:** Bid volume (darker = higher volume)
- **Right side of cells:** Ask volume  
- **Time axis:** Horizontal (newer data on right)
- **Price axis:** Vertical (mid-price in center) 