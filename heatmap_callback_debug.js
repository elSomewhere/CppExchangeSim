// Heatmap Callback Debug Script
// Specifically for diagnosing why heatmap buffer stays at 0 despite L2 events

function debugHeatmapCallbacks() {
    console.log('🔍 === HEATMAP CALLBACK INVESTIGATION ===');
    
    // 1. Check current state
    console.log('\n1. CURRENT STATE:');
    const heatmapEnabled = document.getElementById('heatmapEnabled')?.checked;
    const heatmapBufferSize = document.getElementById('heatmapBufferSize')?.value;
    const heatmapUpdateFreq = document.getElementById('heatmapUpdateFreq')?.value;
    
    console.log(`   Heatmap Enabled: ${heatmapEnabled}`);
    console.log(`   Buffer Size: ${heatmapBufferSize}`);
    console.log(`   Update Frequency: ${heatmapUpdateFreq}`);
    console.log(`   Simulation Running: ${typeof isRunning !== 'undefined' ? isRunning : 'unknown'}`);
    
    // 2. Check if worker exists and force callback registration
    console.log('\n2. FORCING CALLBACK REGISTRATION:');
    if (typeof timingWorker !== 'undefined' && timingWorker !== null) {
        console.log('   📨 Sending force callback registration to worker...');
        timingWorker.postMessage({
            type: 'FORCE_HEATMAP_CALLBACK_REGISTRATION'
        });
        
        // Also request debug info
        setTimeout(() => {
            console.log('   📨 Requesting debug info...');
            timingWorker.postMessage({
                type: 'HEATMAP_DEBUG'
            });
        }, 1000);
        
    } else {
        console.log('   ❌ No timing worker available');
        return 'No worker - cannot debug callbacks';
    }
    
    // 3. Set up temporary enhanced logging
    console.log('\n3. SETTING UP ENHANCED LOGGING:');
    
    // Monitor worker messages
    const originalWorkerOnMessage = timingWorker.onmessage;
    let messageCount = 0;
    
    timingWorker.onmessage = function(event) {
        const { type, data } = event.data;
        messageCount++;
        
        if (type === 'L2_UPDATE') {
            console.log(`🔄 [${messageCount}] L2_UPDATE received:`, data ? 'has data' : 'no data');
            if (data && data.eventType) {
                console.log(`    Event type: ${data.eventType}`);
                console.log(`    Symbol: ${data.symbol || 'unknown'}`);
                console.log(`    Bids: ${data.bids ? data.bids.length : 0}, Asks: ${data.asks ? data.asks.length : 0}`);
            }
        } else if (type === 'HEATMAP_UPDATE') {
            console.log(`🎯 [${messageCount}] HEATMAP_UPDATE received:`, data ? 'has data' : 'no data');
            if (data) {
                console.log(`    Buffer usage: ${data.bufferUsage}/${data.bufferSize}`);
                console.log(`    Mid price: ${data.midPrice}`);
                console.log(`    Levels: ${data.numLevels}`);
                console.log(`    Bid volumes length: ${data.bidVolumes ? data.bidVolumes.length : 0}`);
                console.log(`    Ask volumes length: ${data.askVolumes ? data.askVolumes.length : 0}`);
            }
        } else if (type === 'FORCE_CALLBACK_REGISTRATION_COMPLETE') {
            console.log(`✅ [${messageCount}] Callback registration completed`);
        } else if (type === 'HEATMAP_DEBUG_RESULT') {
            console.log(`📊 [${messageCount}] Debug result:`, data);
        }
        
        // Call original handler
        originalWorkerOnMessage.call(this, event);
    };
    
    // 4. Set optimal settings for immediate testing
    console.log('\n4. SETTING OPTIMAL TEST SETTINGS:');
    
    // Enable heatmap
    if (document.getElementById('heatmapEnabled')) {
        document.getElementById('heatmapEnabled').checked = true;
        console.log('   ✅ Enabled heatmap');
    }
    
    // Set minimal buffer size
    if (document.getElementById('heatmapBufferSize')) {
        document.getElementById('heatmapBufferSize').value = '10'; // Very small for immediate results
        console.log('   ✅ Set buffer size to 10');
    }
    
    // Set frequency to 1
    if (document.getElementById('heatmapUpdateFreq')) {
        document.getElementById('heatmapUpdateFreq').value = '1';
        console.log('   ✅ Set frequency to 1');
    }
    
    // 5. Monitor for next 30 seconds
    console.log('\n5. MONITORING FOR 30 SECONDS:');
    console.log('   Watch for messages with 🔄 (L2) and 🎯 (Heatmap) prefixes');
    
    let l2Count = 0;
    let heatmapCount = 0;
    const startTime = Date.now();
    
    const monitorInterval = setInterval(() => {
        const elapsed = Math.floor((Date.now() - startTime) / 1000);
        if (elapsed >= 30) {
            clearInterval(monitorInterval);
            console.log('\n📈 MONITORING COMPLETE:');
            console.log(`   L2 updates received: ${l2Count}`);
            console.log(`   Heatmap updates received: ${heatmapCount}`);
            
            if (l2Count > 0 && heatmapCount === 0) {
                console.log('   🔍 DIAGNOSIS: L2 events are flowing but heatmap callbacks are not triggered');
                console.log('   💡 LIKELY CAUSES:');
                console.log('      • C++ heatmap hook not properly instantiated');
                console.log('      • WASM binding missing for heatmap callback');
                console.log('      • Buffer not filling due to incorrect configuration');
                console.log('      • Callback registration failed silently');
            } else if (l2Count === 0) {
                console.log('   🔍 DIAGNOSIS: No L2 events being generated');
                console.log('   💡 CHECK: Is simulation actually running and processing events?');
            } else if (heatmapCount > 0) {
                console.log('   ✅ SUCCESS: Both L2 and heatmap callbacks are working!');
            }
            
            // Restore original message handler
            timingWorker.onmessage = originalWorkerOnMessage;
        } else {
            console.log(`   ⏱️  Monitoring... ${elapsed}/30s (L2: ${l2Count}, Heatmap: ${heatmapCount})`);
        }
    }, 5000);
    
    // Count messages
    const originalOnMessage = timingWorker.onmessage;
    timingWorker.onmessage = function(event) {
        const { type } = event.data;
        if (type === 'L2_UPDATE') l2Count++;
        if (type === 'HEATMAP_UPDATE') heatmapCount++;
        originalOnMessage.call(this, event);
    };
    
    return 'Callback debugging started - watch console for 30 seconds';
}

// Quick test function to check WASM binding availability
function checkWASMHeatmapBindings() {
    console.log('🔍 CHECKING WASM HEATMAP BINDINGS:');
    
    if (typeof Module === 'undefined') {
        console.log('   ❌ Module not available globally');
        return 'Module not loaded';
    }
    
    console.log('   ✅ Module available');
    
    // Check for ExchangeSimulation
    if (typeof Module.ExchangeSimulation === 'undefined') {
        console.log('   ❌ ExchangeSimulation not bound');
        return 'ExchangeSimulation missing';
    }
    
    console.log('   ✅ ExchangeSimulation available');
    
    // Try to create instance and check methods
    try {
        const testSim = new Module.ExchangeSimulation();
        console.log('   ✅ ExchangeSimulation instance created');
        
        // Check for heatmap methods
        const heatmapMethods = [
            'setHeatmapCallback',
            'setHeatmapFrequency', 
            'setHeatmapUpdates',
            'setHeatmapBufferSize',
            'getHeatmapEnabled'
        ];
        
        heatmapMethods.forEach(method => {
            if (typeof testSim[method] === 'function') {
                console.log(`   ✅ ${method} available`);
            } else {
                console.log(`   ❌ ${method} missing`);
            }
        });
        
        testSim.delete(); // Clean up
        return 'WASM binding check complete';
        
    } catch (error) {
        console.log(`   ❌ Error creating ExchangeSimulation: ${error.message}`);
        return `Error: ${error.message}`;
    }
}

// Auto-load notification
if (typeof window !== 'undefined') {
    console.log('🔍 Heatmap callback debugging loaded. Run:');
    console.log('   debugHeatmapCallbacks() - Full callback investigation');
    console.log('   checkWASMHeatmapBindings() - Check WASM method availability');
} 