// Quick Fix for Heatmap Display Issues
// Run this script in the browser console if heatmap is not showing

function fixHeatmapSystem() {
    console.log('🔧 APPLYING HEATMAP FIXES...');
    
    // 1. Ensure global onHeatmapUpdate function exists
    if (typeof window.onHeatmapUpdate !== 'function') {
        console.log('✅ Creating missing onHeatmapUpdate function');
        window.onHeatmapUpdate = function(heatmapData) {
            console.log('[HeatmapFix] Heatmap data received:', heatmapData);
            
            if (typeof heatmapViz !== 'undefined' && heatmapViz !== null) {
                heatmapViz.onHeatmapUpdate(heatmapData);
            } else {
                console.warn('[HeatmapFix] heatmapViz not available, storing data for later');
                // Store data for when heatmap visualization is ready
                window.pendingHeatmapData = heatmapData;
            }
            
            // Also try updating volume chart if available
            if (typeof volumeChart !== 'undefined' && volumeChart !== null && volumeChart.onVolumeUpdate) {
                volumeChart.onVolumeUpdate(heatmapData);
            }
        };
    } else {
        console.log('✅ onHeatmapUpdate function already exists');
    }
    
    // 2. Enable heatmap if disabled
    const heatmapCheckbox = document.getElementById('heatmapEnabled');
    if (heatmapCheckbox && !heatmapCheckbox.checked) {
        console.log('✅ Enabling heatmap checkbox');
        heatmapCheckbox.checked = true;
    }
    
    // 3. Set optimal frequency for testing
    const freqInput = document.getElementById('heatmapUpdateFreq');
    if (freqInput && parseInt(freqInput.value) > 5) {
        console.log('✅ Setting optimal heatmap frequency (2)');
        freqInput.value = '2';
    }
    
    // 4. Reduce buffer size for faster initial display
    const bufferInput = document.getElementById('heatmapBufferSize');
    if (bufferInput && parseInt(bufferInput.value) > 100) {
        console.log('✅ Reducing buffer size for faster display (100)');
        bufferInput.value = '100';
    }
    
    // 5. Initialize heatmap visualization if missing
    if (typeof heatmapViz === 'undefined' || heatmapViz === null) {
        console.log('✅ Creating heatmap visualization object');
        try {
            // Check if OrderbookHeatmap class is available
            if (typeof OrderbookHeatmap !== 'undefined') {
                window.heatmapViz = new OrderbookHeatmap();
                console.log('✅ Heatmap visualization created successfully');
                
                // Apply any pending data
                if (typeof window.pendingHeatmapData !== 'undefined') {
                    console.log('✅ Applying pending heatmap data');
                    heatmapViz.onHeatmapUpdate(window.pendingHeatmapData);
                    delete window.pendingHeatmapData;
                }
            } else {
                console.log('⚠️  OrderbookHeatmap class not available - might need heatmap.html');
            }
        } catch (error) {
            console.log(`❌ Error creating heatmap visualization: ${error.message}`);
        }
    } else {
        console.log('✅ Heatmap visualization already exists');
    }
    
    // 6. Force worker to update heatmap configuration
    if (typeof timingWorker !== 'undefined' && timingWorker !== null) {
        console.log('✅ Updating worker heatmap configuration');
        
        // Send updated configuration
        timingWorker.postMessage({
            type: 'SET_HEATMAP_FREQ',
            data: { frequency: parseInt(freqInput?.value || 2) }
        });
        
        // Request immediate debug info
        timingWorker.postMessage({
            type: 'HEATMAP_DEBUG',
            data: {}
        });
    }
    
    // 7. Test the system with synthetic data
    console.log('✅ Testing with synthetic heatmap data');
    
    // Generate test data
    const testHeatmapData = {
        bidVolumes: new Array(200).fill(0).map((_, i) => {
            const distFromCenter = Math.abs(i - 100);
            return Math.max(0, (20 - distFromCenter/5) * Math.random());
        }),
        askVolumes: new Array(200).fill(0).map((_, i) => {
            const distFromCenter = Math.abs(i - 100);
            return Math.max(0, (20 - distFromCenter/5) * Math.random());
        }),
        midPrice: 50000,
        basePrice: 50000,
        tickSize: 1.0,
        numLevels: 200,
        bufferUsage: 50,
        bufferSize: 100,
        timestamp: Date.now(),
        priceLabels: new Array(200).fill(0).map((_, i) => 50000 + (i - 100) * 1.0),
        stats: {
            maxBidVolume: 20,
            maxAskVolume: 20,
            p95BidVolume: 15,
            p95AskVolume: 15
        }
    };
    
    // Apply test data
    if (typeof onHeatmapUpdate === 'function') {
        onHeatmapUpdate(testHeatmapData);
        console.log('✅ Test heatmap data applied successfully');
    }
    
    console.log('🔧 FIXES COMPLETE!');
    console.log('\n📋 WHAT WAS FIXED:');
    console.log('   • Ensured onHeatmapUpdate function exists');
    console.log('   • Enabled heatmap in configuration');
    console.log('   • Set optimal frequency and buffer size');
    console.log('   • Created heatmap visualization if missing');
    console.log('   • Updated worker configuration');
    console.log('   • Applied test data to verify functionality');
    
    console.log('\n🚀 NEXT STEPS:');
    console.log('   1. If using main simulation: restart simulation to apply new settings');
    console.log('   2. If heatmap still not showing: check for JavaScript errors in console');
    console.log('   3. Run debugHeatmapSystem() for detailed diagnosis');
    console.log('   4. Try standalone heatmap.html if main page issues persist');
    
    return 'Heatmap system fixes applied';
}

// Auto-run basic check
if (typeof window !== 'undefined') {
    console.log('🔧 Heatmap fix script loaded. Run fixHeatmapSystem() to apply fixes.');
    
    // Quick auto-check for obvious issues
    const heatmapEnabled = document.getElementById('heatmapEnabled')?.checked;
    if (heatmapEnabled === false) {
        console.log('⚠️  Quick check: Heatmap is disabled. Run fixHeatmapSystem() to enable.');
    }
    
    if (typeof onHeatmapUpdate !== 'function') {
        console.log('⚠️  Quick check: onHeatmapUpdate function missing. Run fixHeatmapSystem() to create.');
    }
} 