// js/automation.js
// Automation page - workflow builder with full functionality
// Uses the global `api` object from core.js

// ============================================================
// Automation State
// ============================================================
let workflowNodes = [];
let workflowConnections = [];
let selectedRuleId = null;
let nextNodeId = 1;

const NODE_TYPES = {
    SENSOR: 'sensor',
    OPERATOR: 'operator',
    RELAY: 'relay',
    EMAIL: 'email'
};

// Connection drag state
const dragState = {
    isDragging: false,
    fromNodeId: null,
    tempLine: null,
    startX: 0,
    startY: 0,
    _boundMove: null,
    _boundEnd: null,
    _boundTouchMove: null,
    _boundTouchEnd: null
};

// ============================================================
// Connection Rules & Validation
// ============================================================

const CONNECTION_RULES = {
    'sensor': { 
        output: true, 
        input: false, 
        maxOutputs: 1,
        maxInputs: 0,
        label: 'Sensor'
    },
    'operator': { 
        output: true, 
        input: true, 
        maxOutputs: 1,
        maxInputs: 4,
        label: 'Operator (AND/OR)'
    },
    'relay': { 
        output: false, 
        input: true, 
        maxOutputs: 0,
        maxInputs: 1,
        label: 'Relay'
    },
    'email': { 
        output: false, 
        input: true, 
        maxOutputs: 0,
        maxInputs: 1,
        label: 'Email'
    }
};

function canConnect(fromNode, toNode) {
    if (!fromNode || !toNode) {
        return { valid: false, reason: 'Invalid nodes' };
    }
    
    const fromRules = CONNECTION_RULES[fromNode.type];
    const toRules = CONNECTION_RULES[toNode.type];
    
    if (!fromRules || !toRules) {
        return { valid: false, reason: 'Unknown node type' };
    }
    
    if (!fromRules.output) {
        return { valid: false, reason: `${fromRules.label} cannot have outputs` };
    }
    
    if (!toRules.input) {
        return { valid: false, reason: `${toRules.label} cannot have inputs` };
    }
    
    const existingOutputs = workflowConnections.filter(c => c.from === fromNode.id).length;
    if (existingOutputs >= fromRules.maxOutputs) {
        return { valid: false, reason: `${fromRules.label} already has max outputs (${fromRules.maxOutputs})` };
    }
    
    const existingInputs = workflowConnections.filter(c => c.to === toNode.id).length;
    if (existingInputs >= toRules.maxInputs) {
        return { valid: false, reason: `${toRules.label} already has max inputs (${toRules.maxInputs})` };
    }
    
    return { valid: true };
}

function isValidConnection(fromType, toType) {
    const validMap = {
        'sensor': ['operator', 'relay'],
        'operator': ['operator', 'relay', 'email'],
        'relay': [],
        'email': []
    };
    return validMap[fromType] ? validMap[fromType].includes(toType) : false;
}

// ============================================================
// Load Automation Elements
// ============================================================
async function loadAutomationElements() {
    try {
        const [sensorData, relayData] = await Promise.all([
            api.get('/api/sensors'),
            api.get('/api/relays')
        ]);
        
        const sensorList = document.getElementById('sensor-list');
        if (sensorList) {
            sensorList.innerHTML = (sensorData.sensors || []).map(s => `
                <div class="element-item sensor" draggable="true" data-type="${NODE_TYPES.SENSOR}" data-id="${s.id}" data-name="${s.name || 'Sensor ' + s.id}" data-pin="${s.pin || '--'}">
                    📊 ${s.name || 'Sensor ' + s.id}
                    <span class="badge">${s.pin !== undefined ? 'PIN ' + s.pin : '--'}</span>
                    <span class="badge status ${s.enabled ? 'active' : 'inactive'}">${s.enabled ? '●' : '○'}</span>
                </div>
            `).join('');
        }
        
        const relayList = document.getElementById('relay-list');
        if (relayList) {
            relayList.innerHTML = (relayData.relays || []).map(r => `
                <div class="element-item output" draggable="true" data-type="${NODE_TYPES.RELAY}" data-id="${r.id}" data-name="${r.name || 'Relay ' + r.id}" data-pin="${r.pin || '--'}" data-duration="${r.activity_duration || 5000}">
                    ⚡ ${r.name || 'Relay ' + r.id}
                    <span class="badge">PIN ${r.pin || '--'}</span>
                    <span class="badge status ${r.active ? 'triggered' : 'idle'}">${r.active ? '⚡' : '○'}</span>
                </div>
            `).join('');
        }
        
    } catch (e) {
        console.warn('Automation elements load failed:', e);
    }
}

// ============================================================
// Load Rules from Server
// ============================================================
async function loadRules() {
    try {
        const response = await api.get('/api/rules');
        const rules = response.rules || [];
        
        const list = document.getElementById('rules-list');
        if (!list) return;
        
        if (rules.length === 0) {
            list.innerHTML = '<p style="color:#7a9bbf; text-align:center; padding:20px;">No rules configured</p>';
            return;
        }
        
        list.innerHTML = rules.map(rule => `
            <div class="rule-item ${rule.enabled ? 'active' : 'disabled'}" data-id="${rule.id}" onclick="loadRuleToCanvas(${rule.id})">
                <div class="rule-name">${rule.name || 'Rule ' + rule.id}</div>
                <div class="rule-desc">${getRuleDescription(rule)}</div>
                <div class="rule-stats">
                    <span>🔄 ${rule.trigger_count || 0} triggers</span>
                    <span>🕐 ${rule.last_triggered ? new Date(rule.last_triggered * 1000).toLocaleString() : 'Never'}</span>
                </div>
                <div class="rule-actions">
                    <button class="enable-btn" onclick="event.stopPropagation(); toggleRule(${rule.id}, true)">Enable</button>
                    <button class="disable-btn" onclick="event.stopPropagation(); toggleRule(${rule.id}, false)">Disable</button>
                    <button class="delete-btn" onclick="event.stopPropagation(); deleteRule(${rule.id})">Delete</button>
                </div>
            </div>
        `).join('');
        
    } catch (e) {
        console.warn('Rules load failed:', e);
    }
}

// ============================================================
// Helper: Get Rule Description
// ============================================================
function getRuleDescription(rule) {
    if (!rule.conditions || !rule.outputs) return 'No conditions defined';
    
    const cond = rule.conditions[0];
    const out = rule.outputs[0];
    if (!cond || !out) return 'Incomplete rule';
    
    const compMap = ['>', '<', '==', '!='];
    const sensorName = `Sensor ${cond.sensor_id}`;
    const relayName = `Relay ${out.id}`;
    
    return `${sensorName} ${compMap[cond.comparator] || '>'} ${cond.threshold} → ${relayName} (${out.duration || 5000} ms)`;
}

// ============================================================
// Add Node
// ============================================================
function addNode(type, sourceId, x, y, params) {
    console.log('Adding node:', { type, sourceId, x, y, params });
    
    if (isNaN(x) || x < 10) x = 100;
    if (isNaN(y) || y < 10) y = 100;
    
    const node = {
        id: nextNodeId++,
        type: type,
        source_id: sourceId || 0,
        x: x,
        y: y,
        params: params || {}
    };
    workflowNodes.push(node);
    console.log('Node added. Total nodes:', workflowNodes.length);
    renderWorkflow();
    return node;
}

// ============================================================
// Render Workflow
// ============================================================
function renderWorkflow() {
    const dropZone = document.getElementById('canvas-drop-zone');
    if (!dropZone) {
        console.error('Canvas drop zone not found');
        return;
    }
    
    console.log('Rendering workflow. Nodes:', workflowNodes.length);
    
    dropZone.innerHTML = '';
    dropZone.style.cssText = `
        position: relative;
        width: 100%;
        height: 300px;
        min-height: 300px;
        border: 2px dashed #dde6ef;
        border-radius: 12px;
        background: #fafcff;
        overflow: visible;
        padding: 0;
        margin: 0;
    `;
    
    if (workflowNodes.length === 0) {
        const placeholder = document.createElement('div');
        placeholder.style.cssText = `
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100%;
            color: #9e9e9e;
            font-size: 0.95rem;
        `;
        placeholder.innerHTML = `
            <p>Drag elements here to build workflows</p>
            <p style="font-size:0.75rem; color:#7a9bbf; margin-top:8px;">
                🔗 To connect: <strong>click and drag</strong> the ➤ arrow on a node
            </p>
        `;
        dropZone.appendChild(placeholder);
        return;
    }
    
    const container = document.createElement('div');
    container.style.cssText = `
        position: relative;
        width: 100%;
        height: 100%;
        min-height: 300px;
        overflow: visible;
    `;
    
    // SVG layer for connections
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('style', 'position:absolute; top:0; left:0; width:100%; height:100%; pointer-events:none; z-index:1;');
    
    // Draw connections
    workflowConnections.forEach(conn => {
        const fromNode = workflowNodes.find(n => n.id === conn.from);
        const toNode = workflowNodes.find(n => n.id === conn.to);
        if (!fromNode || !toNode) return;
        
        // ============================================================
        // Calculate connection points dynamically
        // ============================================================
        
        // SOURCE: Right side of the node (where the + button is)
        // The node width is ~140px (120px min-width + 20px padding)
        // The + button is at right: -10px, so we use node.x + node width
        const fromX = fromNode.x + 140 -15;  // Right edge of the node
        const fromY = fromNode.y + 30 + 15;   // Vertically centered (half of ~60px height)
        
        // TARGET: Left side of the node (where the connection should enter)
        // The + button is NOT on the target, so we use the left edge
        const toX = toNode.x;            // Left edge of the node
        const toY = toNode.y + 30 + 15;       // Vertically centered
        
        // ============================================================
        // Draw the line
        // ============================================================
        const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
        line.setAttribute('x1', fromX);
        line.setAttribute('y1', fromY);
        line.setAttribute('x2', toX);
        line.setAttribute('y2', toY);
        line.setAttribute('stroke', '#2b7be4');
        line.setAttribute('stroke-width', '2');
        line.setAttribute('stroke-dasharray', '5,3');
        line.dataset.from = conn.from;
        line.dataset.to = conn.to;
        svg.appendChild(line);
        
        // ============================================================
        // Draw the arrow aligned with the line
        // ============================================================
        const angle = Math.atan2(toY - fromY, toX - fromX);
        const arrowSize = 10;
        
        // Arrow tip at the target (left edge of node)
        const tipX = toX;
        const tipY = toY;
        
        // Arrow base (8px before the tip, along the line)
        const baseX = tipX - 8 * Math.cos(angle);
        const baseY = tipY - 8 * Math.sin(angle);
        
        const marker = document.createElementNS('http://www.w3.org/2000/svg', 'polygon');
        marker.setAttribute('points', 
            `${tipX},${tipY} ` +
            `${baseX - arrowSize * Math.cos(angle - 0.5)},${baseY - arrowSize * Math.sin(angle - 0.5)} ` +
            `${baseX - arrowSize * Math.cos(angle + 0.5)},${baseY - arrowSize * Math.sin(angle + 0.5)}`
        );
        marker.setAttribute('fill', '#2b7be4');
        svg.appendChild(marker);
                
        // ============================================================
        // Make connection clickable for deletion
        // ============================================================
        const clickArea = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
        const midX = (fromX + toX) / 2;
        const midY = (fromY + toY) / 2;
        const length = Math.sqrt(Math.pow(toX - fromX, 2) + Math.pow(toY - fromY, 2));
        
        clickArea.setAttribute('x', midX - length/2);
        clickArea.setAttribute('y', midY - 8);
        clickArea.setAttribute('width', length);
        clickArea.setAttribute('height', '16');
        clickArea.setAttribute('transform', `rotate(${angle * 180 / Math.PI}, ${midX}, ${midY})`);
        clickArea.setAttribute('fill', 'transparent');
        clickArea.style.cursor = 'pointer';
        clickArea.style.pointerEvents = 'visible';
        clickArea.dataset.from = conn.from;
        clickArea.dataset.to = conn.to;
        
        clickArea.addEventListener('click', function(e) {
            e.stopPropagation();
            const fromId = parseInt(this.dataset.from);
            const toId = parseInt(this.dataset.to);
            if (confirm('Delete this connection?')) {
                deleteConnection(fromId, toId);
            }
        });
        clickArea.addEventListener('mouseenter', function() {
            const svgEl = this.closest('svg');
            if (svgEl) {
                svgEl.querySelectorAll('line').forEach(l => {
                    if (parseInt(l.dataset.from) === parseInt(this.dataset.from) && 
                        parseInt(l.dataset.to) === parseInt(this.dataset.to)) {
                        l.setAttribute('stroke', '#ff4444');
                        l.setAttribute('stroke-width', '4');
                    }
                });
                svgEl.querySelectorAll('polygon').forEach(p => {
                    if (p.dataset.from === this.dataset.from && p.dataset.to === this.dataset.to) {
                        p.setAttribute('fill', '#ff4444');
                    }
                });
            }
        });
        clickArea.addEventListener('mouseleave', function() {
            const svgEl = this.closest('svg');
            if (svgEl) {
                svgEl.querySelectorAll('line').forEach(l => {
                    if (parseInt(l.dataset.from) === parseInt(this.dataset.from) && 
                        parseInt(l.dataset.to) === parseInt(this.dataset.to)) {
                        l.setAttribute('stroke', '#2b7be4');
                        l.setAttribute('stroke-width', '2');
                    }
                });
                svgEl.querySelectorAll('polygon').forEach(p => {
                    if (p.dataset.from === this.dataset.from && p.dataset.to === this.dataset.to) {
                        p.setAttribute('fill', '#2b7be4');
                    }
                });
            }
        });
        
        svg.appendChild(clickArea);
    });    
    container.appendChild(svg);
    
    // Create nodes
    workflowNodes.forEach(node => {
        const el = document.createElement('div');
        el.setAttribute('data-node-id', node.id);
        el.className = 'workflow-node';
        
        let title = 'Node';
        let body = '';
        let borderColor = '#2b7be4';
        let bgColor = '#e8f0fe';
        
        switch (node.type) {
            case NODE_TYPES.SENSOR:
                title = node.params.name || 'Sensor ' + node.source_id;
                body = `${node.params.comparator || '>'} ${node.params.threshold || 0}`;
                borderColor = '#2b7be4';
                bgColor = '#e8f0fe';
                break;
            case NODE_TYPES.OPERATOR:
                title = node.params.operator || 'AND';
                body = 'Logical operator';
                borderColor = '#f9a825';
                bgColor = '#fff8e1';
                break;
            case NODE_TYPES.RELAY:
                title = node.params.name || 'Relay ' + node.source_id;
                body = `Duration: ${node.params.duration || 5000}ms`;
                borderColor = '#43a047';
                bgColor = '#e8f5e9';
                break;
            case NODE_TYPES.EMAIL:
                title = '📧 Email';
                body = 'Send notification';
                borderColor = '#e91e63';
                bgColor = '#fce4ec';
                break;
            default:
                title = node.type;
                body = '';
        }
        
        // ============================================================
        // Conditionally add connect button ONLY for nodes that can output
        // ============================================================
        const canHaveOutput = CONNECTION_RULES[node.type]?.output === true;
        
        let innerHTML = `
            <div style="font-weight:600; font-size:0.9rem; color:${borderColor};">${title}</div>
            <div style="font-size:0.75rem; color:#3d6a9b; margin-top:4px;">${body}</div>
            <div style="margin-top:6px; display:flex; gap:4px; flex-wrap:wrap;">
                <button class="edit-node-btn" data-node-id="${node.id}" style="padding:1px 8px; font-size:0.6rem; border:none; border-radius:4px; cursor:pointer; background:#d4edda; color:#155724;">✏️</button>
                <button class="delete-node-btn" data-node-id="${node.id}" style="padding:1px 8px; font-size:0.6rem; border:none; border-radius:4px; cursor:pointer; background:#f8d7da; color:#721c24;">✖</button>
        `;
        
        // ONLY add connect button if this node can have outputs
        if (canHaveOutput) {
            innerHTML += `
                <button class="connect-btn" data-node-id="${node.id}" style="
                    position: absolute;
                    right: -10px;
                    top: 50%;
                    transform: translateY(-50%);
                    width: 22px;
                    height: 22px;
                    border-radius: 50%;
                    border: 2px solid #fff;
                    background: ${borderColor};
                    color: #fff;
                    font-size: 14px;
                    font-weight: bold;
                    cursor: grab;
                    box-shadow: 0 2px 8px rgba(0,0,0,0.2);
                    display: flex;
                    align-items: center;
                    justify-content: center;
                    padding: 0;
                    line-height: 1;
                    transition: all 0.2s;
                    z-index: 20;
                " title="Drag to connect to another node">+</button>
            `;
        }
        
        innerHTML += `</div>`; // Close the button container
        
        el.innerHTML = innerHTML;
        
        // Position the node
        el.style.cssText = `
            position: absolute;
            left: ${node.x}px;
            top: ${node.y}px;
            min-width: 120px;
            max-width: 180px;
            padding: 12px 16px;
            border-radius: 10px;
            border: 2px solid ${borderColor};
            background: ${bgColor};
            box-shadow: 0 4px 12px rgba(0,0,0,0.08);
            cursor: move;
            user-select: none;
            font-size: 0.8rem;
            z-index: 10;
            pointer-events: auto;
            overflow: visible;
        `;
        
        // ============================================================
        // Attach event listener for connect button (if it exists)
        // ============================================================
        const connectBtn = el.querySelector('.connect-btn');
        if (connectBtn) {
            connectBtn.addEventListener('mousedown', function(e) {
                console.log('🔴 CONNECT BUTTON MOUSEDOWN! Starting connection from node:', node.id);
                e.stopPropagation();
                e.preventDefault();
                startConnectionDrag(e.clientX, e.clientY, node.id);
            });
            
            connectBtn.addEventListener('mouseenter', function() {
                this.style.transform = 'translateY(-50%) scale(1.15)';
                this.style.boxShadow = '0 0 20px rgba(43,123,228,0.6)';
            });
            connectBtn.addEventListener('mouseleave', function() {
                this.style.transform = 'translateY(-50%) scale(1)';
                this.style.boxShadow = '0 2px 8px rgba(0,0,0,0.2)';
            });
        }
        
        // --- Edit button ---
        const editBtn = el.querySelector('.edit-node-btn');
        if (editBtn) {
            editBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                editNode(parseInt(this.dataset.nodeId));
            });
        }
        
        // --- Delete button ---
        const deleteBtn = el.querySelector('.delete-node-btn');
        if (deleteBtn) {
            deleteBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                deleteNode(parseInt(this.dataset.nodeId));
            });
        }
        
        // --- Drag to move ---
        el.addEventListener('mousedown', function(e) {
            if (e.target.closest('button')) return;
            
            const rect = this.getBoundingClientRect();
            const offsetX = e.clientX - rect.left;
            const offsetY = e.clientY - rect.top;
            
            function onMouseMove(e) {
                const canvasRect = dropZone.getBoundingClientRect();
                node.x = Math.max(0, e.clientX - canvasRect.left - offsetX);
                node.y = Math.max(0, e.clientY - canvasRect.top - offsetY);
                el.style.left = node.x + 'px';
                el.style.top = node.y + 'px';
                renderWorkflow();
            }
            
            function onMouseUp() {
                document.removeEventListener('mousemove', onMouseMove);
                document.removeEventListener('mouseup', onMouseUp);
            }
            
            document.addEventListener('mousemove', onMouseMove);
            document.addEventListener('mouseup', onMouseUp);
        });
        
        container.appendChild(el);
    });
    
    dropZone.appendChild(container);
}

// ============================================================
// Connection Drag Functions
// ============================================================

function startConnectionDrag(clientX, clientY, nodeId) {
    console.log('🔵 startConnectionDrag called:', { clientX, clientY, nodeId });
    
    if (dragState.isDragging) {
        showNotification('Already connecting', 'warning');
        return;
    }
    
    const sourceNode = workflowNodes.find(n => n.id === nodeId);
    if (!sourceNode) {
        showNotification('Source node not found', 'error');
        return;
    }
    
    if (!CONNECTION_RULES[sourceNode.type]?.output) {
        showNotification(`${sourceNode.type} cannot have outputs`, 'error');
        return;
    }
    
    const existingOutputs = workflowConnections.filter(c => c.from === nodeId).length;
    const maxOutputs = CONNECTION_RULES[sourceNode.type]?.maxOutputs || 1;
    if (existingOutputs >= maxOutputs) {
        showNotification(`Max outputs reached (${maxOutputs})`, 'warning');
        return;
    }
    
    // Initialize drag state
    dragState.isDragging = true;
    dragState.fromNodeId = nodeId;
    dragState.startX = clientX;
    dragState.startY = clientY;
    
    // Change cursor
    document.body.style.cursor = 'grabbing';
    
    // Create temp line (this follows the mouse)
    dragState.tempLine = document.createElement('div');
    dragState.tempLine.style.cssText = `
        position: fixed;
        height: 3px;
        background: #2b7be4;
        z-index: 9999;
        pointer-events: none;
        transform-origin: left center;
        box-shadow: 0 0 10px rgba(43,123,228,0.5);
        border-radius: 2px;
    `;
    document.body.appendChild(dragState.tempLine);
    
    // Update line position
    updateTempLine(clientX, clientY);
    
    showNotification('Drag to destination node to connect', 'info');
    
    // Bind event listeners
    dragState._boundMove = onDragMove;
    dragState._boundEnd = onDragEnd;
    dragState._boundTouchMove = onDragTouchMove;
    dragState._boundTouchEnd = onDragTouchEnd;
    
    document.addEventListener('mousemove', dragState._boundMove);
    document.addEventListener('mouseup', dragState._boundEnd);
    document.addEventListener('touchmove', dragState._boundTouchMove, { passive: false });
    document.addEventListener('touchend', dragState._boundTouchEnd);
}

function onDragMove(e) {
    if (!dragState.isDragging) return;
    e.preventDefault();
    updateTempLine(e.clientX, e.clientY);
    highlightDropTarget(e.clientX, e.clientY);
}

function onDragTouchMove(e) {
    if (!dragState.isDragging) return;
    e.preventDefault();
    const touch = e.touches[0];
    if (touch) {
        updateTempLine(touch.clientX, touch.clientY);
        highlightDropTarget(touch.clientX, touch.clientY);
    }
}

function onDragEnd(e) {
    if (!dragState.isDragging) return;
    finishConnection(e.clientX, e.clientY);
}

function onDragTouchEnd(e) {
    if (!dragState.isDragging) return;
    const touch = e.changedTouches[0];
    if (touch) {
        finishConnection(touch.clientX, touch.clientY);
    }
}

function updateTempLine(clientX, clientY) {
    const tempLine = dragState.tempLine;
    if (!tempLine || dragState.fromNodeId === null) return;
    
    const dropZone = document.getElementById('canvas-drop-zone');
    if (!dropZone) return;
    
    const containerRect = dropZone.getBoundingClientRect();
    const sourceNode = workflowNodes.find(n => n.id === dragState.fromNodeId);
    if (!sourceNode) return;
    
    // Calculate source position (right side of the node)
    const sourceX = containerRect.left + sourceNode.x + 140 - 15;
    const sourceY = containerRect.top + sourceNode.y + 30 + 15;
    
    const dx = clientX - sourceX;
    const dy = clientY - sourceY;
    const length = Math.sqrt(dx * dx + dy * dy);
    const angle = Math.atan2(dy, dx);
    
    tempLine.style.left = sourceX + 'px';
    tempLine.style.top = sourceY + 'px';
    tempLine.style.width = length + 'px';
    tempLine.style.transform = `rotate(${angle}rad)`;
}

function highlightDropTarget(clientX, clientY) {
    // Remove previous highlights
    document.querySelectorAll('.drop-target-highlight').forEach(el => {
        el.classList.remove('drop-target-highlight');
        el.style.borderColor = '';
        el.style.boxShadow = '';
    });
    
    const element = document.elementFromPoint(clientX, clientY);
    if (!element) return;
    
    const nodeEl = element.closest('.workflow-node');
    if (!nodeEl) return;
    
    const targetNodeId = parseInt(nodeEl.dataset.nodeId);
    if (targetNodeId === dragState.fromNodeId) return;
    
    const targetNode = workflowNodes.find(n => n.id === targetNodeId);
    if (!targetNode) return;
    
    const fromNode = workflowNodes.find(n => n.id === dragState.fromNodeId);
    if (!fromNode) return;
    
    // Check if target can accept inputs
    if (!CONNECTION_RULES[targetNode.type]?.input) {
        nodeEl.style.borderColor = '#ff4444';
        nodeEl.style.boxShadow = '0 0 20px rgba(255,68,68,0.3)';
        nodeEl.classList.add('drop-target-highlight');
        return;
    }
    
    // Check if target already has max inputs
    const existingInputs = workflowConnections.filter(c => c.to === targetNodeId).length;
    const maxInputs = CONNECTION_RULES[targetNode.type]?.maxInputs || 1;
    if (existingInputs >= maxInputs) {
        nodeEl.style.borderColor = '#ff8800';
        nodeEl.style.boxShadow = '0 0 20px rgba(255,136,0,0.3)';
        nodeEl.classList.add('drop-target-highlight');
        return;
    }
    
    // Check if connection is valid
    const validation = canConnect(fromNode, targetNode);
    if (validation.valid) {
        nodeEl.style.borderColor = '#4caf50';
        nodeEl.style.boxShadow = '0 0 20px rgba(76,175,80,0.4)';
        nodeEl.classList.add('drop-target-highlight');
    } else {
        nodeEl.style.borderColor = '#ff4444';
        nodeEl.style.boxShadow = '0 0 20px rgba(255,68,68,0.3)';
        nodeEl.classList.add('drop-target-highlight');
    }
}

function finishConnection(clientX, clientY) {
    // Remove temp line
    if (dragState.tempLine) {
        dragState.tempLine.remove();
        dragState.tempLine = null;
    }
    
    document.querySelectorAll('.drop-target-highlight').forEach(el => {
        el.classList.remove('drop-target-highlight');
        el.style.borderColor = '';
        el.style.boxShadow = '';
    });
    
    document.body.style.cursor = '';
    
    if (dragState.fromNodeId === null) {
        cleanupDragState();
        return;
    }
    
    const element = document.elementFromPoint(clientX, clientY);
    if (!element) {
        cleanupDragState();
        showNotification('Connection cancelled', 'info');
        return;
    }
    
    const nodeEl = element.closest('.workflow-node');
    if (!nodeEl) {
        cleanupDragState();
        showNotification('Connection cancelled', 'info');
        return;
    }
    
    const targetNodeId = parseInt(nodeEl.dataset.nodeId);
    if (targetNodeId === dragState.fromNodeId) {
        cleanupDragState();
        showNotification('Cannot connect node to itself', 'error');
        return;
    }
    
    const fromNode = workflowNodes.find(n => n.id === dragState.fromNodeId);
    const targetNode = workflowNodes.find(n => n.id === targetNodeId);
    
    if (!fromNode || !targetNode) {
        cleanupDragState();
        showNotification('Node not found', 'error');
        return;
    }
    
    // Check if target can accept inputs
    if (!CONNECTION_RULES[targetNode.type]?.input) {
        cleanupDragState();
        showNotification(`${targetNode.type} cannot accept inputs`, 'error');
        return;
    }
    
    // Check max inputs
    const existingInputs = workflowConnections.filter(c => c.to === targetNodeId).length;
    const maxInputs = CONNECTION_RULES[targetNode.type]?.maxInputs || 1;
    if (existingInputs >= maxInputs) {
        cleanupDragState();
        showNotification(`Target already has max inputs (${maxInputs})`, 'warning');
        return;
    }
    
    const validation = canConnect(fromNode, targetNode);
    if (!validation.valid) {
        cleanupDragState();
        showNotification(validation.reason || 'Invalid connection', 'error');
        return;
    }
    
    const exists = workflowConnections.some(c => c.from === dragState.fromNodeId && c.to === targetNodeId);
    if (exists) {
        cleanupDragState();
        showNotification('Connection already exists', 'warning');
        return;
    }
    
    // No prompt - just add the connection with a simple default condition
    workflowConnections.push({
        from: dragState.fromNodeId,
        to: targetNodeId,
        condition: '→'  // Simple arrow, or you can use an empty string
    });
    
    renderWorkflow();
    showNotification(`✅ Connected: ${fromNode.params.name || fromNode.type} → ${targetNode.params.name || targetNode.type}`, 'success');
    
    cleanupDragState();
}

function cleanupDragState() {
    dragState.isDragging = false;
    dragState.fromNodeId = null;
    dragState.startX = 0;
    dragState.startY = 0;
    document.body.style.cursor = '';
    
    if (dragState._boundMove) {
        document.removeEventListener('mousemove', dragState._boundMove);
        document.removeEventListener('mouseup', dragState._boundEnd);
        document.removeEventListener('touchmove', dragState._boundTouchMove);
        document.removeEventListener('touchend', dragState._boundTouchEnd);
        dragState._boundMove = null;
        dragState._boundEnd = null;
        dragState._boundTouchMove = null;
        dragState._boundTouchEnd = null;
    }
}

// ============================================================
// Delete Connection
// ============================================================
function deleteConnection(fromId, toId) {
    workflowConnections = workflowConnections.filter(
        c => !(c.from === fromId && c.to === toId)
    );
    renderWorkflow();
    showNotification('Connection removed', 'info');
}

// ============================================================
// Node Management Functions
// ============================================================
function deleteNode(nodeId) {
    if (!confirm('Remove this node?')) return;
    workflowNodes = workflowNodes.filter(n => n.id !== nodeId);
    workflowConnections = workflowConnections.filter(c => c.from !== nodeId && c.to !== nodeId);
    renderWorkflow();
    showNotification('Node deleted', 'info');
}

function editNode(nodeId) {
    const node = workflowNodes.find(n => n.id === nodeId);
    if (!node) return;
    
    let newParams = { ...node.params };
    let changed = false;
    
    switch (node.type) {
        case NODE_TYPES.SENSOR:
            const newThreshold = prompt('Enter threshold value:', node.params.threshold);
            if (newThreshold !== null) {
                newParams.threshold = parseFloat(newThreshold);
                changed = true;
            }
            const newComparator = prompt('Enter comparator (>, <, ==, !=):', node.params.comparator);
            if (newComparator !== null) {
                newParams.comparator = newComparator;
                changed = true;
            }
            break;
        case NODE_TYPES.OPERATOR:
            const newOp = prompt('Enter operator (AND, OR, NOT):', node.params.operator);
            if (newOp !== null) {
                newParams.operator = newOp.toUpperCase();
                changed = true;
            }
            break;
        case NODE_TYPES.RELAY:
            const newDuration = prompt('Enter duration (miliseconds):', node.params.duration);
            if (newDuration !== null) {
                newParams.duration = parseInt(newDuration) || 5000;
                changed = true;
            }
            break;
        case NODE_TYPES.EMAIL:
            const newRecipient = prompt('Enter recipient email:', node.params.recipient || '');
            if (newRecipient !== null) {
                newParams.recipient = newRecipient;
                changed = true;
            }
            break;
    }
    
    if (changed) {
        node.params = newParams;
        renderWorkflow();
        showNotification('Node updated', 'success');
    }
}

// ============================================================
// Rule Management Functions
// ============================================================
async function toggleRule(ruleId, enabled) {
    try {
        const response = await api.get('/api/rules');
        const rule = (response.rules || []).find(r => r.id === ruleId);
        if (!rule) {
            showNotification('Rule not found', 'error');
            return;
        }

        const updatedRule = { ...rule, enabled: enabled ? 1 : 0 };
        const result = await api.post('/api/rules', updatedRule);
        if (result.status === 'success' || result.rule_id !== undefined) {
            showNotification(`Rule ${enabled ? 'enabled' : 'disabled'}`, 'success');
            await loadRules();
        }
    } catch (e) {
        console.warn('Toggle rule failed:', e);
        showNotification('Failed to toggle rule: ' + e.message, 'error');
    }
}

async function deleteRule(ruleId) {
    if (!confirm('Delete this rule?')) return;
    
    try {
        showNotification('Deleting rule...', 'info');
        
        const response = await delete(`/api/rules/delete?id=${ruleId}`);
        
        if (!response.ok) {
            throw new Error('Delete failed: ' + response.status);
        }
        
        const result = await response.json();
        console.log('Delete result:', result);
        
        if (result.status === 'success') {
            showNotification('Rule deleted successfully', 'success');
            await loadRules();
            
            if (selectedRuleId === ruleId) {
                selectedRuleId = null;
                clearCanvas();
                loadRules();
            }
        } else {
            showNotification('Failed to delete rule: ' + (result.message || 'Unknown error'), 'error');
        }
    } catch (e) {
        console.warn('Delete rule failed:', e);
        showNotification('Error deleting rule: ' + e.message, 'error');
    }
}

function clearCanvas() {
    if (workflowNodes.length === 0 && workflowConnections.length === 0) return;
    if (!confirm('Clear all nodes and connections?')) return;
    workflowNodes = [];
    workflowConnections = [];
    selectedRuleId = null;
    document.getElementById('rule-name-input').value = '';
    renderWorkflow();
    showNotification('Canvas cleared', 'info');
}

function testRule() {
    if (workflowNodes.length === 0) {
        showNotification('No workflow to test', 'warning');
        return;
    }
    
    let desc = [];
    workflowConnections.forEach(conn => {
        const fromNode = workflowNodes.find(n => n.id === conn.from);
        const toNode = workflowNodes.find(n => n.id === conn.to);
        if (fromNode && toNode) {
            const fromLabel = fromNode.params.name || fromNode.type;
            const toLabel = toNode.params.name || toNode.type;
            desc.push(`${fromLabel} ${conn.condition || '→'} ${toLabel}`);
        }
    });
    
    const message = `🧪 Testing Rule\n\nNodes: ${workflowNodes.length}\nConnections: ${workflowConnections.length}\n\n${desc.join('\n') || 'No connections defined'}`;
    alert(message);
}

// ============================================================
// Validate Workflow - Simplified (NO NOT)
// Supports:
//   - 1 Sensor → Relay/Email
//   - Multiple Sensors → AND/OR → Relay/Email
// ============================================================
function validateWorkflow() {
    const errors = [];
    const warnings = [];
    
    // ============================================================
    // 1. Basic node counts
    // ============================================================
    const sensors = workflowNodes.filter(n => n.type === NODE_TYPES.SENSOR);
    const relays = workflowNodes.filter(n => n.type === NODE_TYPES.RELAY);
    const operators = workflowNodes.filter(n => n.type === NODE_TYPES.OPERATOR);
    const emails = workflowNodes.filter(n => n.type === NODE_TYPES.EMAIL);
    
    // ============================================================
    // 2. Must have at least one sensor and one output
    // ============================================================
    if (sensors.length === 0) {
        errors.push('• No sensor found - add at least one sensor');
    }
    if (relays.length === 0 && emails.length === 0) {
        errors.push('• No output found - add at least one relay or email');
    }
    
    // ============================================================
    // 3. Check operator rules
    // ============================================================
    const andOrOperators = operators.filter(op => {
        const opType = op.params.operator || 'AND';
        return opType === 'AND' || opType === 'OR';
    });
    
    // 3a. Only one operator allowed
    if (andOrOperators.length > 1) {
        errors.push(`• Only one AND/OR operator allowed (found ${andOrOperators.length})`);
    }
    
    // 3b. If operator exists, validate it
    andOrOperators.forEach(op => {
        const opType = op.params.operator || 'AND';
        const inputs = workflowConnections.filter(c => c.to === op.id);
        const outputs = workflowConnections.filter(c => c.from === op.id);
        
        // Count sensor inputs
        const sensorInputs = inputs.filter(c => {
            const from = workflowNodes.find(n => n.id === c.from);
            return from && from.type === NODE_TYPES.SENSOR;
        });
        
        if (sensorInputs.length < 2) {
            errors.push(`• ${opType} operator needs at least 2 sensor inputs (has ${sensorInputs.length})`);
        }
        
        // Check if any input is from another operator
        const operatorInputs = inputs.filter(c => {
            const from = workflowNodes.find(n => n.id === c.from);
            return from && from.type === NODE_TYPES.OPERATOR;
        });
        if (operatorInputs.length > 0) {
            errors.push(`• ${opType} operator cannot have another operator as input`);
        }
        
        // Check outputs
        if (outputs.length > 1) {
            errors.push(`• ${opType} operator can only have 1 output (has ${outputs.length})`);
        }
        
        // Validate output destination
        outputs.forEach(out => {
            const to = workflowNodes.find(n => n.id === out.to);
            if (to) {
                if (to.type === NODE_TYPES.SENSOR) {
                    errors.push(`• ${opType} operator output cannot go to a sensor`);
                }
                if (to.type === NODE_TYPES.OPERATOR) {
                    errors.push(`• ${opType} operator output cannot go to another operator`);
                }
            }
        });
    });
    
    // ============================================================
    // 4. Check direct sensor→relay connections
    // ============================================================
    const directSensorRelayConnections = workflowConnections.filter(c => {
        const from = workflowNodes.find(n => n.id === c.from);
        const to = workflowNodes.find(n => n.id === c.to);
        return from && from.type === NODE_TYPES.SENSOR && 
               to && to.type === NODE_TYPES.RELAY;
    });
    
    // 4a. If there's an operator, no direct sensor→relay connections allowed
    if (andOrOperators.length > 0 && directSensorRelayConnections.length > 0) {
        errors.push('• When using AND/OR operator, all sensors must connect to the operator (no direct sensor→relay connections)');
    }
    
    // 4b. If no operator, only ONE sensor→relay connection allowed
    if (andOrOperators.length === 0 && directSensorRelayConnections.length > 1) {
        errors.push('• Multiple direct sensor→relay connections without operator - use separate rules or add an operator');
    }
    
    // ============================================================
    // 5. Check for isolated nodes
    // ============================================================
    const isolatedNodes = workflowNodes.filter(node => {
        const hasInput = workflowConnections.some(c => c.to === node.id);
        const hasOutput = workflowConnections.some(c => c.from === node.id);
        return !hasInput && !hasOutput;
    });
    isolatedNodes.forEach(node => {
        const name = node.params.name || node.type;
        errors.push(`• "${name}" is isolated (no connections)`);
    });
    
    // ============================================================
    // 6. Check for dangling connections
    // ============================================================
    workflowConnections.forEach(conn => {
        const fromNode = workflowNodes.find(n => n.id === conn.from);
        const toNode = workflowNodes.find(n => n.id === conn.to);
        if (!fromNode || !toNode) {
            errors.push(`• Dangling connection: source or target node not found`);
        }
    });
    
    // ============================================================
    // 7. Check for invalid connection types
    // ============================================================
    workflowConnections.forEach(conn => {
        const fromNode = workflowNodes.find(n => n.id === conn.from);
        const toNode = workflowNodes.find(n => n.id === conn.to);
        if (!fromNode || !toNode) return;
        
        if (!isValidConnection(fromNode.type, toNode.type)) {
            errors.push(`• Invalid connection: ${fromNode.type} → ${toNode.type}`);
        }
    });
    
    // ============================================================
    // 8. Check output count (single output rule)
    // ============================================================
    const totalOutputs = relays.length + emails.length;
    if (totalOutputs > 1) {
        errors.push(`• Only 1 output allowed (relay or email) - found ${totalOutputs} outputs`);
    }
    
    // ============================================================
    // 9. Check for unused sensors (no outgoing connection)
    // ============================================================
    sensors.forEach(sensor => {
        const hasOutput = workflowConnections.some(c => c.from === sensor.id);
        if (!hasOutput) {
            const name = sensor.params.name || `Sensor ${sensor.source_id}`;
            errors.push(`• "${name}" has no outgoing connection`);
        }
    });
    
    // ============================================================
    // 10. Check for unused relays (no incoming connection)
    // ============================================================
    relays.forEach(relay => {
        const hasInput = workflowConnections.some(c => c.to === relay.id);
        if (!hasInput) {
            const name = relay.params.name || `Relay ${relay.source_id}`;
            errors.push(`• "${name}" has no incoming connection`);
        }
    });
    
    // ============================================================
    // 11. Check for unused emails (no incoming connection)
    // ============================================================
    emails.forEach(email => {
        const hasInput = workflowConnections.some(c => c.to === email.id);
        if (!hasInput) {
            errors.push(`• Email node has no incoming connection`);
        }
    });
    
    // ============================================================
    // 12. Generate summary
    // ============================================================
    const hasErrors = errors.length > 0;
    const hasWarnings = warnings.length > 0;
    
    let description = '';
    if (!hasErrors) {
        if (andOrOperators.length === 1) {
            const opType = andOrOperators[0].params.operator || 'AND';
            description = `${sensors.length} sensors → ${opType} → ${relays.length > 0 ? 'relay' : 'email'}`;
        } else {
            description = `${sensors.length} sensor(s) → ${relays.length > 0 ? 'relay' : 'email'}`;
        }
    }
    
    return {
        valid: !hasErrors,
        errors: errors,
        warnings: warnings,
        description: description,
        stats: {
            sensors: sensors.length,
            relays: relays.length,
            operators: operators.length,
            emails: emails.length,
            connections: workflowConnections.length,
            hasAndOrOperator: andOrOperators.length > 0
        }
    };
}

async function saveRule() {
    if (workflowNodes.length === 0) {
        showNotification('Please add at least one node', 'warning');
        return;
    }
    
    // Validate workflow
    const validation = validateWorkflow();
    
    if (!validation.valid) {
        const errorMsg = `❌ Cannot save rule - please fix the following:\n\n${validation.errors.join('\n')}\n\nSupported patterns:\n• 1 Sensor → Relay/Email\n• Multiple Sensors → AND → Relay/Email\n• Multiple Sensors → OR → Relay/Email\n\nAll sensors must connect to the operator when using AND/OR.\nOnly 1 output (relay or email) allowed per rule.`;
        alert(errorMsg);
        showNotification('Workflow has errors - please fix them', 'error');
        return;
    }
    
    // Build rule from workflow
    const ruleName = document.getElementById('rule-name-input')?.value?.trim() || 'Rule ' + (Date.now() % 1000);
    
    const sensors = workflowNodes.filter(n => n.type === NODE_TYPES.SENSOR);
    const relays = workflowNodes.filter(n => n.type === NODE_TYPES.RELAY);
    const operators = workflowNodes.filter(n => n.type === NODE_TYPES.OPERATOR);
    const emails = workflowNodes.filter(n => n.type === NODE_TYPES.EMAIL);
    
    const conditions = [];
    const outputs = [];
    const compMap = {'>': 0, '<': 1, '==': 2, '!=': 3};
    let logicType = 0; // AND by default
    
    // Determine logic type from operator
    const andOrOperators = operators.filter(op => {
        const opType = op.params.operator || 'AND';
        return opType === 'AND' || opType === 'OR';
    });
    
    if (andOrOperators.length === 1) {
        const opType = andOrOperators[0].params.operator || 'AND';
        logicType = opType === 'OR' ? 1 : 0;
    }
    
    // Build conditions from sensors
    sensors.forEach(sensor => {
        conditions.push({
            sensor_id: sensor.source_id || 0,
            comparator: compMap[sensor.params.comparator || '>'] || 0,
            threshold: parseFloat(sensor.params.threshold) || 0
        });
    });
    
    // Build outputs
    if (relays.length > 0) {
        const relay = relays[0];
        outputs.push({
            type: 0,
            id: relay.source_id || 0,
            duration: parseInt(relay.params.duration) || 5000
        });
    }
    
    if (emails.length > 0) {
        const email = emails[0];
        outputs.push({
            type: 1,
            id: 0,
            duration: 0
        });
    }
    
    // Build the rule
    const rule = {
        name: ruleName,
        enabled: 1,
        logic_type: logicType,
        cooldown_seconds: 10,
        conditions: conditions,
        outputs: outputs
    };
    
    if (emails.length > 0) {
        const email = emails[0];
        rule.email_recipient = email.params.recipient || 'admin@example.com';
        rule.email_subject = 'WQMS Alert: ' + ruleName;
    }
    
    // Check if this is an update
    const isUpdate = (selectedRuleId !== null && selectedRuleId !== undefined);
    
    if (isUpdate) {
        if (!confirm(`⚠️ Rule "${ruleName}" (ID: ${selectedRuleId}) already exists.\n\nDo you want to overwrite it?`)) {
            showNotification('Save cancelled', 'info');
            return;
        }
        rule.id = selectedRuleId;
        console.log('🔄 UPDATING rule ID:', selectedRuleId);
    } else {
        console.log('🆕 CREATING new rule');
    }
    
    // Save
    try {
        const result = await api.post('/api/rules', rule);
        console.log('📨 Save result:', result);
        
        if (result.status === 'success' || result.rule_id !== undefined) {
            const action = isUpdate ? 'updated' : 'created';
            showNotification(`✅ Rule "${ruleName}" ${action} successfully! (${validation.description})`, 'success');
            await loadRules();
            selectedRuleId = null;
        } else {
            showNotification('Failed to save rule: ' + (result.message || 'Unknown error'), 'error');
        }
    } catch (e) {
        console.warn('Save rule failed:', e);
        showNotification('Error saving rule: ' + e.message, 'error');
    }
}

async function loadRuleToCanvas(ruleId) {
    try {
        const response = await api.get('/api/rules');
        const rule = (response.rules || []).find(r => r.id === ruleId);
        if (!rule) {
            showNotification('Rule not found', 'error');
            return;
        }
        
        // Clear canvas
        workflowNodes = [];
        workflowConnections = [];
        selectedRuleId = ruleId;
        document.getElementById('rule-name-input').value = rule.name || 'Rule ' + ruleId;
        
        // Get sensor and relay data
        let sensorData = { sensors: [] };
        let relayData = { relays: [] };
        try {
            [sensorData, relayData] = await Promise.all([
                api.get('/api/sensors'),
                api.get('/api/relays')
            ]);
        } catch (e) {
            console.warn('Failed to load sensor/relay names:', e);
        }
        
        const sensors = sensorData.sensors || [];
        const relays = relayData.relays || [];
        
        const conditions = rule.conditions || [];
        const outputs = rule.outputs || [];
        const logicType = rule.logic_type || 0;
        
        if (conditions.length === 0 || outputs.length === 0) {
            showNotification('Rule has no conditions or outputs', 'warning');
            renderWorkflow();
            return;
        }
        
        const compMap = ['>', '<', '==', '!='];
        const hasAndOrOperator = conditions.length > 1;
        
        // Layout parameters
        const canvasWidth = 600;
        const leftX = 20;
        const rightX = canvasWidth - 240;
        const centerX = (leftX + rightX) / 2;
        const startY = 20;
        const nodeSpacing = 100;
        
        const sensorCount = conditions.length;
        const relayCount = outputs.length;
        const rowCount = Math.max(sensorCount, relayCount);
        
        // Create sensor nodes (stacked vertically)
        const sensorNodes = [];
        const relayNodes = [];
        
        conditions.forEach((cond, index) => {
            const sensorId = cond.sensor_id || 0;
            const sensor = sensors.find(s => s.id === sensorId);
            const sensorName = sensor ? sensor.name : `Sensor ${sensorId}`;
            
            const yOffset = (rowCount - sensorCount) * nodeSpacing / 2;
            
            const node = {
                id: nextNodeId++,
                type: NODE_TYPES.SENSOR,
                source_id: sensorId,
                x: leftX,
                y: startY + yOffset + (index * nodeSpacing),
                params: {
                    name: sensorName,
                    threshold: cond.threshold || 0,
                    comparator: compMap[cond.comparator] || '>'
                }
            };
            sensorNodes.push(node);
            workflowNodes.push(node);
        });
        
        // Create relay nodes (stacked vertically)
        outputs.forEach((out, index) => {
            const relayId = out.id || 0;
            const relay = relays.find(r => r.id === relayId);
            const relayName = relay ? relay.name : `Relay ${relayId}`;
            
            const yOffset = (rowCount - relayCount) * nodeSpacing / 2;
            
            const node = {
                id: nextNodeId++,
                type: NODE_TYPES.RELAY,
                source_id: relayId,
                x: rightX,
                y: startY + yOffset + (index * nodeSpacing),
                params: {
                    name: relayName,
                    duration: out.duration || 5000
                }
            };
            relayNodes.push(node);
            workflowNodes.push(node);
        });
        
        // Create operator node if multiple sensors
        let operatorNode = null;
        if (hasAndOrOperator && sensorCount > 1) {
            const opName = logicType === 1 ? 'OR' : 'AND';
            const opY = startY + (rowCount - 1) * nodeSpacing / 2;
            operatorNode = {
                id: nextNodeId++,
                type: NODE_TYPES.OPERATOR,
                source_id: 0,
                x: centerX,
                y: opY,
                params: {
                    operator: opName
                }
            };
            workflowNodes.push(operatorNode);
        }
        
        // Create connections
        if (operatorNode) {
            // All sensors → operator
            sensorNodes.forEach((sensor, index) => {
                const cond = conditions[index] || conditions[0];
                workflowConnections.push({
                    from: sensor.id,
                    to: operatorNode.id,
                    condition: `${compMap[cond.comparator] || '>'} ${cond.threshold || 0}`
                });
            });
            
            // Operator → all relays
            relayNodes.forEach(relay => {
                workflowConnections.push({
                    from: operatorNode.id,
                    to: relay.id,
                    condition: `→`
                });
            });
        } else {
            // Direct connections (paired)
            const pairCount = Math.min(sensorNodes.length, relayNodes.length);
            for (let i = 0; i < pairCount; i++) {
                const sensorNode = sensorNodes[i];
                const relayNode = relayNodes[i];
                const cond = conditions[i] || conditions[0];
                
                if (sensorNode && relayNode) {
                    workflowConnections.push({
                        from: sensorNode.id,
                        to: relayNode.id,
                        condition: `${compMap[cond.comparator] || '>'} ${cond.threshold || 0}`
                    });
                }
            }
        }
        
        // Handle email outputs
        const emailOutputs = outputs.filter(out => out.type === 1);
        if (emailOutputs.length > 0) {
            const emailY = startY + (rowCount) * nodeSpacing + 40;
            const emailNode = {
                id: nextNodeId++,
                type: NODE_TYPES.EMAIL,
                source_id: 0,
                x: rightX,
                y: emailY,
                params: {
                    recipient: rule.email_recipient || 'admin@example.com'
                }
            };
            workflowNodes.push(emailNode);
            
            if (operatorNode) {
                workflowConnections.push({
                    from: operatorNode.id,
                    to: emailNode.id,
                    condition: `→`
                });
            } else if (relayNodes.length > 0) {
                workflowConnections.push({
                    from: relayNodes[relayNodes.length - 1].id,
                    to: emailNode.id,
                    condition: `→`
                });
            }
        }
        
        renderWorkflow();
        showNotification(`✅ Rule "${rule.name}" loaded`, 'success');
        
    } catch (e) {
        console.warn('Load rule failed:', e);
        showNotification('Failed to load rule: ' + e.message, 'error');
    }
}

// ============================================================
// Drag & Drop Handlers for Adding Nodes
// ============================================================
document.addEventListener('dragstart', function(e) {
    const el = e.target.closest('.element-item');
    if (el) {
        const data = {
            type: el.dataset.type,
            id: el.dataset.id || '0',
            name: el.dataset.name || el.textContent.trim(),
            pin: el.dataset.pin || '--',
            duration: el.dataset.duration || '5000'
        };
        e.dataTransfer.setData('application/json', JSON.stringify(data));
        e.dataTransfer.effectAllowed = 'copy';
    }
});

document.addEventListener('dragover', function(e) {
    const dropZone = e.target.closest('#canvas-drop-zone');
    if (dropZone) {
        e.preventDefault();
        e.dataTransfer.dropEffect = 'copy';
        dropZone.style.borderColor = '#2b7be4';
        dropZone.style.backgroundColor = '#f0f7ff';
    }
});

document.addEventListener('dragleave', function(e) {
    const dropZone = e.target.closest('#canvas-drop-zone');
    if (dropZone) {
        dropZone.style.borderColor = '#dde6ef';
        dropZone.style.backgroundColor = '#fafcff';
    }
});

document.addEventListener('drop', function(e) {
    const dropZone = e.target.closest('#canvas-drop-zone');
    if (!dropZone) return;
    e.preventDefault();
    
    dropZone.style.borderColor = '#dde6ef';
    dropZone.style.backgroundColor = '#fafcff';
    
    try {
        const data = JSON.parse(e.dataTransfer.getData('application/json'));
        const rect = dropZone.getBoundingClientRect();
        const dropZoneWidth = dropZone.clientWidth || 600;
        const dropZoneHeight = dropZone.clientHeight || 300;
        
        let x = e.clientX - rect.left - 60;
        let y = e.clientY - rect.top - 30;
        x = Math.max(10, Math.min(x, dropZoneWidth - 150));
        y = Math.max(10, Math.min(y, dropZoneHeight - 80));
        
        console.log('Drop data:', data);
        console.log('Drop position:', { x, y, dropZoneWidth, dropZoneHeight });
        
        let params = {};
        
        if (data.type === NODE_TYPES.SENSOR) {
            const threshold = prompt('Enter threshold value:', '7.5');
            if (threshold === null) return;
            const comparator = prompt('Enter comparator (>, <, ==, !=):', '>');
            if (comparator === null) return;
            params = { 
                name: data.name, 
                threshold: parseFloat(threshold), 
                comparator: comparator 
            };
        } else if (data.type === NODE_TYPES.OPERATOR) {
            params = { operator: data.name || 'AND' };
        } else if (data.type === NODE_TYPES.RELAY) {
            const duration = prompt('Enter duration (miliseconds):', data.duration || '5000');
            if (duration === null) return;
            params = { 
                name: data.name, 
                duration: parseInt(duration) || 5000
            };
        } else if (data.type === NODE_TYPES.EMAIL) {
            const recipient = prompt('Enter recipient email:', 'admin@example.com');
            if (recipient === null) return;
            params = { 
                type: 'email',
                recipient: recipient 
            };
        } else {
            showNotification('Unknown element type', 'error');
            return;
        }
        
        addNode(data.type, parseInt(data.id) || 0, x, y, params);
        showNotification(`Added ${data.type} node`, 'success');
        
    } catch (err) {
        console.warn('Drop failed:', err);
        showNotification('Failed to add element: ' + err.message, 'error');
    }
});

// ============================================================
// Notification System
// ============================================================
function showNotification(message, type = 'info') {
    let container = document.getElementById('notification-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'notification-container';
        container.style.cssText = `
            position: fixed;
            top: 80px;
            right: 20px;
            z-index: 99999;
            max-width: 400px;
            display: flex;
            flex-direction: column;
            gap: 8px;
        `;
        document.body.appendChild(container);
    }
    
    const colors = {
        success: '#d4edda',
        error: '#f8d7da',
        warning: '#fff3cd',
        info: '#d1ecf1'
    };
    const textColors = {
        success: '#155724',
        error: '#721c24',
        warning: '#856404',
        info: '#0c5460'
    };
    
    const notification = document.createElement('div');
    notification.style.cssText = `
        background: ${colors[type] || colors.info};
        color: ${textColors[type] || textColors.info};
        padding: 12px 20px;
        border-radius: 8px;
        border: 1px solid ${textColors[type] || textColors.info};
        box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        font-size: 0.9rem;
        animation: slideIn 0.3s ease;
        opacity: 1;
        transition: opacity 0.3s;
    `;
    notification.textContent = message;
    
    container.appendChild(notification);
    setTimeout(() => {
        notification.style.opacity = '0';
        setTimeout(() => notification.remove(), 300);
    }, 3000);
}

// ============================================================
// Export Rules (JSON)
// ============================================================
async function exportRules() {
    try {
        showNotification('Exporting rules...', 'info');
        
        const response = await fetch('/api/rules/export');
        if (!response.ok) {
            throw new Error('Export failed: ' + response.status);
        }
        
        const jsonData = await response.json();
        const jsonString = JSON.stringify(jsonData, null, 2);
        
        // Create download link
        const blob = new Blob([jsonString], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'rules_export_' + new Date().toISOString().slice(0,10) + '.json';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        showNotification('Rules exported successfully!', 'success');
    } catch (e) {
        console.warn('Export failed:', e);
        showNotification('Export failed: ' + e.message, 'error');
    }
}

// ============================================================
// Import Rules (JSON)
// ============================================================
async function importRules() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    
    input.onchange = async function(e) {
        const file = e.target.files[0];
        if (!file) return;
        
        try {
            showNotification('Importing rules...', 'info');
            
            const reader = new FileReader();
            reader.onload = async function(event) {
                try {
                    const jsonData = event.target.result;
                    
                    const response = await fetch('/api/rules/import', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: jsonData
                    });
                    
                    if (!response.ok) {
                        throw new Error('Import failed: ' + response.status);
                    }
                    
                    const result = await response.json();
                    showNotification(`Imported ${result.imported || 0} rules successfully!`, 'success');
                    
                    await loadRules();
                    await loadAutomationElements();
                    
                } catch (err) {
                    console.warn('Import failed:', err);
                    showNotification('Import failed: ' + err.message, 'error');
                }
            };
            reader.readAsText(file);
            
        } catch (e) {
            console.warn('Import failed:', e);
            showNotification('Import failed: ' + e.message, 'error');
        }
    };
    
    input.click();
}

// ============================================================
// Initialize Automation
// ============================================================
async function initAutomation() {
    await updateHeader();
    await loadAutomationElements();
    await loadRules();

    document.getElementById('save-rule')?.addEventListener('click', saveRule);
    document.getElementById('clear-canvas')?.addEventListener('click', clearCanvas);
    document.getElementById('test-rule')?.addEventListener('click', testRule);
    document.getElementById('add-rule')?.addEventListener('click', function() {
        if (typeof clearCanvas === 'function') clearCanvas();
        document.getElementById('rule-name-input').value = 'New Rule ' + (Date.now() % 1000);
        if (typeof selectedRuleId !== 'undefined') selectedRuleId = null;
    });
    document.getElementById('delete-rule')?.addEventListener('click', function() {
        if (selectedRuleId !== null) {
            deleteRule(selectedRuleId);
        }
    });
    document.getElementById('export-rules')?.addEventListener('click', exportRules);
    document.getElementById('import-rules')?.addEventListener('click', importRules);

    renderWorkflow();
    
//    setInterval(() => {
//        loadAutomationElements();
//    }, 15000);

    // Auto-refresh header
    setInterval(updateHeader, REFRESH_INTERVAL);
}

// ============================================================
// Expose Functions to Global Scope
// ============================================================
window.deleteNode = deleteNode;
window.editNode = editNode;
window.loadRuleToCanvas = loadRuleToCanvas;
window.toggleRule = toggleRule;
window.deleteRule = deleteRule;
window.saveRule = saveRule;
window.clearCanvas = clearCanvas;
window.testRule = testRule;
window.loadRules = loadRules;
window.showNotification = showNotification;
window.selectedRuleId = selectedRuleId;
window.workflowNodes = workflowNodes;
window.workflowConnections = workflowConnections;
window.exportRules = exportRules;
window.importRules = importRules;

// ============================================================
// Initialize when DOM is ready
// ============================================================
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initAutomation);
} else {
    initAutomation();
}