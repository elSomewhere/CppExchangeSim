// Comprehensive Heatmap Debugging Script
// Copy and paste this into the browser console while the simulation is running

function debugHeatmapSystem() {
    console.log('=== HEATMAP SYSTEM DIAGNOSTIC ===');
    
    // 1. Check basic configuration
    console.log('\n1. CONFIGURATION CHECK:');
    const heatmapEnabled = document.getElementById('heatmapEnabled')?.checked;
    const heatmapBufferSize = document.getElementById('heatmapBufferSize')?.value;
    const heatmapUpdateFreq = document.getElementById('heatmapUpdateFreq')?.value;
    
    console.log(`   Heatmap Enabled: ${heatmapEnabled}`);
    console.log(`   Buffer Size: ${heatmapBufferSize}`);
    console.log(`   Update Frequency: ${heatmapUpdateFreq}`);
    
    // 2. Check worker and simulation state
    console.log('\n2. SIMULATION STATE:');
    console.log(`   Worker exists: ${typeof timingWorker !== 'undefined' && timingWorker !== null}`);
    console.log(`   Is running: ${typeof isRunning !== 'undefined' ? isRunning : 'unknown'}`);
    console.log(`   Timing active: ${typeof timingLoopActive !== 'undefined' ? timingLoopActive : 'unknown'}`);
    console.log(`   Is initialized: ${typeof isInitialized !== 'undefined' ? isInitialized : 'unknown'}`);
    
    // 3. Check heatmap visualization object
    console.log('\n3. VISUALIZATION STATE:');
    console.log(`   heatmapViz exists: ${typeof heatmapViz !== 'undefined' && heatmapViz !== null}`);
    if (typeof heatmapViz !== 'undefined' && heatmapViz !== null) {
        console.log(`   Data buffer length: ${heatmapViz.dataBuffer ? heatmapViz.dataBuffer.length : 'no buffer'}`);
        console.log(`   Update count: ${heatmapViz.updateCount || 0}`);
        console.log(`   Is playing: ${heatmapViz.isPlaying}`);
    }
    
    // 4. Test worker debug function
    console.log('\n4. REQUESTING WORKER DEBUG INFO:');
    if (typeof timingWorker !== 'undefined' && timingWorker !== null) {
        timingWorker.postMessage({
            type: 'HEATMAP_DEBUG',
            data: {}
        });
        console.log('   Debug request sent to worker - check console for response');
    } else {
        console.log('   ❌ No timing worker available');
    }
    
    // 5. Check for common issues
    console.log('\n5. COMMON ISSUES CHECK:');
    
    if (!heatmapEnabled) {
        console.log('   ❌ ISSUE: Heatmap is disabled in UI');
    }
    
    if (parseInt(heatmapUpdateFreq) > 10) {
        console.log('   ⚠️  WARNING: Update frequency is high - might need many events');
    }
    
    if (parseInt(heatmapBufferSize) > 500) {
        console.log('   ⚠️  WARNING: Buffer size is large - needs many snapshots before first heatmap');
    }
    
    // 6. Test callback functionality
    console.log('\n6. TESTING CALLBACK CHAIN:');
    console.log('   Testing global onHeatmapUpdate function...');
    
    if (typeof onHeatmapUpdate === 'function') {
        console.log('   ✅ onHeatmapUpdate function exists');
        
        // Test with dummy data
        try {
            const testData = {
                bidVolumes: new Array(200).fill(0).map(() => Math.random() * 10),
                askVolumes: new Array(200).fill(0).map(() => Math.random() * 10),
                midPrice: 50000,
                basePrice: 50000,
                tickSize: 1.0,
                numLevels: 200,
                bufferUsage: 1,
                bufferSize: 300,
                timestamp: Date.now(),
                priceLabels: new Array(200).fill(0).map((_, i) => 50000 + (i - 100) * 1.0)
            };
            
            onHeatmapUpdate(testData);
            console.log('   ✅ Test heatmap data processed successfully');
            
        } catch (error) {
            console.log(`   ❌ Error in onHeatmapUpdate: ${error.message}`);
        }
    } else {
        console.log('   ❌ onHeatmapUpdate function does not exist');
    }
    
    // 7. Memory and performance check
    console.log('\n7. PERFORMANCE CHECK:');
    if (typeof performance !== 'undefined') {
        console.log(`   Available memory: ${navigator.deviceMemory || 'unknown'} GB`);
        console.log(`   Timing: ${performance.now().toFixed(2)}ms since page load`);
    }
    
    console.log('\n=== DIAGNOSTIC COMPLETE ===');
    console.log('\nNEXT STEPS:');
    console.log('1. If heatmap is disabled, enable it and restart simulation');
    console.log('2. If frequency/buffer size is high, reduce them and restart');
    console.log('3. If worker debug shows no callbacks, check C++ callback registration');
    console.log('4. If test data works but real data doesn\'t, check data format compatibility');
    console.log('5. Try running testHeatmapFrequency() to temporarily set frequency to 1');
    
    return 'Diagnostic complete - check console output above';
}

// Quick frequency test function
function testHeatmapFrequency() {
    console.log('[HeatmapTest] Testing with frequency = 1 for immediate updates...');
    
    if (typeof timingWorker !== 'undefined' && timingWorker !== null) {
        // Store original frequency
        const originalFreq = document.getElementById('heatmapUpdateFreq')?.value || 5;
        
        // Set frequency to 1
        timingWorker.postMessage({
            type: 'SET_HEATMAP_FREQ',
            data: { frequency: 1 }
        });
        
        console.log('[HeatmapTest] Frequency set to 1 - heatmap should update every event');
        console.log('[HeatmapTest] Watch for "Heatmap data received" messages in console');
        
        // Restore original frequency after 30 seconds
        setTimeout(() => {
            timingWorker.postMessage({
                type: 'SET_HEATMAP_FREQ',
                data: { frequency: parseInt(originalFreq) }
            });
            console.log(`[HeatmapTest] Frequency restored to ${originalFreq}`);
        }, 30000);
        
        return 'Frequency test started - will restore in 30 seconds';
    } else {
        return 'No timing worker available for frequency test';
    }
}

// Check if we're in the right context
if (typeof window !== 'undefined') {
    console.log('Heatmap debugging functions loaded. Run:');
    console.log('  debugHeatmapSystem() - Full diagnostic');
    console.log('  testHeatmapFrequency() - Quick frequency test');
} else {
    console.log('Script loaded but not in browser context');
} 