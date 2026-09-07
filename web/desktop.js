// Start Menu
function toggleStartMenu() {
    document.getElementById('start-menu').classList.toggle('hidden');
}

// Clock
setInterval(() => {
    const d = new Date();
    document.getElementById('clock').innerText = d.toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'});
}, 1000);

// Window Management
let zIndex = 100;
let windows = {};
let windowCounter = 0;

function createWindow(title, contentHTML, setupCallback) {
    const template = document.getElementById('window-template');
    const clone = template.content.cloneNode(true);
    const win = clone.querySelector('.window');
    const winId = 'win-' + (++windowCounter);
    win.id = winId;
    
    // Default size & pos
    win.style.top = (50 + (Math.random() * 50)) + 'px';
    win.style.left = (100 + (Math.random() * 100)) + 'px';
    win.style.zIndex = ++zIndex;
    
    win.querySelector('.window-title').innerText = title;
    win.querySelector('.window-content').innerHTML = contentHTML;
    
    // Add to taskbar
    const taskbarApps = document.getElementById('taskbar-apps');
    const taskItem = document.createElement('div');
    taskItem.className = 'taskbar-app active';
    taskItem.innerText = title;
    taskItem.onclick = () => {
        if (win.classList.contains('minimized')) {
            win.classList.remove('minimized');
            taskItem.classList.add('active');
            win.style.zIndex = ++zIndex;
        } else if (win.style.zIndex == zIndex) {
            win.classList.add('minimized');
            taskItem.classList.remove('active');
        } else {
            win.style.zIndex = ++zIndex;
            taskItem.classList.add('active');
        }
    };
    taskbarApps.appendChild(taskItem);
    windows[winId] = { el: win, taskBtn: taskItem };
    
    // Controls
    win.querySelector('.window-close').onclick = () => {
        win.remove();
        taskItem.remove();
        delete windows[winId];
    };
    
    win.querySelector('.window-minimize').onclick = () => {
        win.classList.add('minimized');
        taskItem.classList.remove('active');
    };
    
    let isMaximized = false;
    win.querySelector('.window-maximize').onclick = () => {
        isMaximized = !isMaximized;
        if (isMaximized) win.classList.add('maximized');
        else win.classList.remove('maximized');
    };
    
    // Bring to front on click
    win.onmousedown = () => {
        win.style.zIndex = ++zIndex;
        document.querySelectorAll('.taskbar-app').forEach(el => el.classList.remove('active'));
        taskItem.classList.add('active');
    };
    
    // Dragging
    const titlebar = win.querySelector('.window-titlebar');
    let isDragging = false, startX, startY, initialX, initialY;
    
    titlebar.onmousedown = (e) => {
        if (isMaximized) return;
        isDragging = true;
        startX = e.clientX;
        startY = e.clientY;
        initialX = win.offsetLeft;
        initialY = win.offsetTop;
        document.onmousemove = (e) => {
            if(!isDragging) return;
            win.style.left = (initialX + e.clientX - startX) + 'px';
            win.style.top = (initialY + e.clientY - startY) + 'px';
        };
        document.onmouseup = () => {
            isDragging = false;
            document.onmousemove = null;
            document.onmouseup = null;
        };
    };
    
    // Resizing
    const resizer = win.querySelector('.window-resize-handle');
    let isResizing = false, initialW, initialH;
    resizer.onmousedown = (e) => {
        if (isMaximized) return;
        isResizing = true;
        startX = e.clientX;
        startY = e.clientY;
        initialW = win.offsetWidth;
        initialH = win.offsetHeight;
        document.onmousemove = (e) => {
            if(!isResizing) return;
            win.style.width = (initialW + e.clientX - startX) + 'px';
            win.style.height = (initialH + e.clientY - startY) + 'px';
        };
        document.onmouseup = () => {
            isResizing = false;
            document.onmousemove = null;
            document.onmouseup = null;
        };
        e.stopPropagation(); // prevent window drag/focus from messing up
    };
    
    document.getElementById('desktop').appendChild(win);
    
    if (setupCallback) setupCallback(win);
}

// Applications
function openApp(appId) {
    document.getElementById('start-menu').classList.add('hidden');
    
    if (appId === 'terminal') {
        const html = `
            <div class="terminal-output"></div>
            <div class="terminal-input-wrapper">
                <span>zerux:/#</span>
                <input type="text" class="terminal-input" autocomplete="off" spellcheck="false">
            </div>
        `;
        createWindow('Terminal', html, (win) => {
            const out = win.querySelector('.terminal-output');
            const inp = win.querySelector('.terminal-input');
            inp.focus();
            
            out.textContent = "Connecting to ZeruX WebSocket...\n";
            
            // Connect WebSocket
            const ws = new WebSocket('ws://' + window.location.host + '/ws/terminal');
            
            ws.onopen = () => {
                out.textContent += "Connected.\n";
            };
            
            ws.onmessage = (e) => {
                out.textContent += e.data;
                out.scrollTop = out.scrollHeight;
            };
            
            ws.onclose = () => {
                out.textContent += "\nConnection closed.\n";
            };
            
            // Terminal input history is handled by kernel now (via ANSI escapes)
            // We just need to capture up/down and send ANSI escapes!
            inp.onkeydown = (e) => {
                if (e.key === 'ArrowUp') {
                    ws.send('\x1B[A');
                    e.preventDefault();
                } else if (e.key === 'ArrowDown') {
                    ws.send('\x1B[B');
                    e.preventDefault();
                } else if (e.key === 'Tab') {
                    ws.send('\t');
                    e.preventDefault();
                } else if (e.key === 'Backspace') {
                    ws.send('\x08'); // Backspace
                    e.preventDefault();
                }
            };
            
            inp.onkeypress = (e) => {
                if (e.key === 'Enter') {
                    ws.send('\r');
                    inp.value = '';
                } else {
                    ws.send(e.key);
                }
                e.preventDefault(); // kernel echoes back
            };
        });
    }
    
    else if (appId === 'explorer') {
        const html = `
            <div class="explorer-layout">
                <div class="explorer-sidebar">
                    <div class="explorer-sidebar-item active" onclick="loadFiles('/disk/fat0', this)">
                        📁 FAT32 Disk (fat0)
                    </div>
                    <div class="explorer-sidebar-item" onclick="loadFiles('/root', this)">
                        🏠 Root
                    </div>
                    <div class="explorer-sidebar-item" onclick="loadFiles('/disk/fat0/etc', this)">
                        ⚙️ System (/etc)
                    </div>
                </div>
                <div class="explorer-main">
                    <!-- Files go here -->
                    <div style="color: #888;">Loading...</div>
                </div>
            </div>
        `;
        createWindow('File Explorer', html, (win) => {
            const main = win.querySelector('.explorer-main');
            
            window.loadFiles = (path, el) => {
                // Update active state
                win.querySelectorAll('.explorer-sidebar-item').forEach(i => i.classList.remove('active'));
                if(el) el.classList.add('active');
                
                main.innerHTML = '<div style="color: #888;">Loading...</div>';
                
                fetch('/api/files' + path)
                    .then(res => res.json())
                    .then(files => {
                        main.innerHTML = '';
                        if (files.length === 0) {
                            main.innerHTML = '<div style="color: #888;">Empty directory.</div>';
                            return;
                        }
                        files.forEach(f => {
                            if (f.name === '.' || f.name === '..') return;
                            
                            const div = document.createElement('div');
                            div.className = 'file-item';
                            div.ondblclick = () => {
                                if (f.name.toLowerCase().endsWith('.txt') || f.name.toLowerCase().endsWith('.cfg') || f.name.toLowerCase().endsWith('.js') || f.name.toLowerCase().endsWith('.css') || f.name.toLowerCase().endsWith('.htm')) {
                                    openEditor(path + '/' + f.name);
                                } else {
                                    alert('Cannot open this file type visually yet.');
                                }
                            };
                            
                            let icon = '📄';
                            if (f.name.toLowerCase().endsWith('.txt')) icon = '📝';
                            if (f.name.toLowerCase().endsWith('.htm')) icon = '🌐';
                            if (f.name.toLowerCase().endsWith('.cfg')) icon = '⚙️';
                            if (!f.name.includes('.')) icon = '📁'; // rough dir check
                            
                            div.innerHTML = `
                                <div class="file-icon">${icon}</div>
                                <div class="file-name">${f.name}</div>
                            `;
                            main.appendChild(div);
                        });
                    })
                    .catch(e => {
                        main.innerHTML = `<div style="color: #ef4444;">Error loading files: ${e}</div>`;
                    });
            };
            
            // Load default
            window.loadFiles('/disk/fat0', win.querySelector('.explorer-sidebar-item'));
        });
    }
    
    else if (appId === 'editor') {
        openEditor();
    }
    
    else if (appId === 'control') {
        const html = `
            <div class="control-layout">
                <div class="control-sidebar">
                    <div class="control-sidebar-item active" onclick="showControlTab('network', this)">Network (DHCP)</div>
                    <div class="control-sidebar-item" onclick="showControlTab('firewall', this)">Firewall / IDS</div>
                    <div class="control-sidebar-item" onclick="showControlTab('logs', this)">System Logs (dmesg)</div>
                    <div class="control-sidebar-item" onclick="showControlTab('services', this)">Services</div>
                    <div class="control-sidebar-item" onclick="showControlTab('security', this)">Network Monitor</div>
                </div>
                <div class="control-main" id="control-content">
                    Loading...
                </div>
            </div>
        `;
        createWindow('ZeruX Control Center', html, (win) => {
            const content = win.querySelector('#control-content');
            
            window.showControlTab = (tab, el) => {
                win.querySelectorAll('.control-sidebar-item').forEach(i => i.classList.remove('active'));
                if (el) el.classList.add('active');
                
                content.innerHTML = 'Loading...';
                
                if (tab === 'network') {
                    // Try to load current net.cfg
                    fetch('/api/file/disk/fat0/etc/network/net.cfg').then(r=>r.text()).then(text => {
                        let isDhcp = text.includes('DHCP=1');
                        content.innerHTML = `
                            <h3>Network Settings</h3>
                            <div class="form-group">
                                <label>Mode</label>
                                <select id="net-mode" onchange="document.getElementById('static-opts').style.display = this.value === '1' ? 'none' : 'block'">
                                    <option value="1" ${isDhcp ? 'selected' : ''}>Automatic (DHCP)</option>
                                    <option value="0" ${!isDhcp ? 'selected' : ''}>Manual (Static)</option>
                                </select>
                            </div>
                            <div id="static-opts" style="display: ${isDhcp ? 'none' : 'block'}">
                                <div class="form-group"><label>IP Address</label><input type="text" id="net-ip" value="192.168.50.2"></div>
                                <div class="form-group"><label>Subnet Mask</label><input type="text" id="net-mask" value="255.255.255.0"></div>
                                <div class="form-group"><label>Gateway</label><input type="text" id="net-gw" value="192.168.50.1"></div>
                            </div>
                            <button class="editor-btn" onclick="saveNetwork()">Save & Restart Network</button>
                        `;
                        
                        window.saveNetwork = () => {
                            let mode = document.getElementById('net-mode').value;
                            let ip = document.getElementById('net-ip').value;
                            let mask = document.getElementById('net-mask').value;
                            let gw = document.getElementById('net-gw').value;
                            let cfg = \`DHCP=\${mode}\\nIP=\${ip}\\nMASK=\${mask}\\nGW=\${gw}\\nDNS=8.8.8.8\\n\`;
                            fetch('/api/file/disk/fat0/etc/network/net.cfg', {method:'POST', body:cfg}).then(() => {
                                fetch('/api/shell', {method:'POST', body:'service network restart'});
                                alert('Network settings saved and stack restarted.');
                            });
                        };
                    });
                } else if (tab === 'firewall') {
                    const renderFirewall = () => {
                        fetch('/api/firewall').then(r=>r.text()).then(rules => {
                            content.innerHTML = `
                                <h3>Firewall / IDS</h3>
                                <div style="background:#1e1e1e; padding:10px; border-radius:5px; margin-bottom:10px; white-space:pre; font-family:monospace; height:150px; overflow-y:auto;">${rules || 'No rules'}</div>
                                <div style="display:flex; gap:10px; align-items:center;">
                                    <input type="text" id="fw-ip" placeholder="Target IP (e.g. 192.168.50.1)">
                                    <button class="editor-btn" onclick="addFw()">Block IP (DROP)</button>
                                </div>
                                <div style="display:flex; gap:10px; align-items:center; margin-top:10px;">
                                    <input type="text" id="fw-id" placeholder="Rule ID">
                                    <button class="editor-btn" onclick="delFw()">Delete Rule</button>
                                </div>
                            `;
                            window.addFw = () => {
                                let ip = document.getElementById('fw-ip').value;
                                if(ip) fetch('/api/firewall/add', {method:'POST', body:ip}).then(()=>renderFirewall());
                            };
                            window.delFw = () => {
                                let id = document.getElementById('fw-id').value;
                                if(id) fetch('/api/firewall/del', {method:'POST', body:id}).then(()=>renderFirewall());
                            };
                        });
                    };
                    renderFirewall();
                } else if (tab === 'logs') {
                    content.innerHTML = `
                        <h3>System Logs (dmesg)</h3>
                        <div id="dmesg-out" style="background:#1e1e1e; padding:10px; border-radius:5px; white-space:pre-wrap; font-family:monospace; height:300px; overflow-y:auto; font-size:12px;">Loading...</div>
                        <button class="editor-btn" onclick="fetchDmesg()" style="margin-top:10px;">Refresh Logs</button>
                    `;
                    window.fetchDmesg = () => {
                        fetch('/api/dmesg').then(r=>r.text()).then(log => {
                            const out = document.getElementById('dmesg-out');
                            if(out) { out.textContent = log; out.scrollTop = out.scrollHeight; }
                        });
                    };
                    window.fetchDmesg();
                } else if (tab === 'services') {
                    content.innerHTML = `
                        <h3>Services</h3>
                        <div class="service-item">
                            <span>GUI Desktop</span>
                            <button class="editor-btn" onclick="fetch('/api/shell', {method:'POST', body:'service desktop stop'})">Stop</button>
                        </div>
                        <div class="service-item">
                            <span>Network Stack</span>
                            <button class="editor-btn" onclick="fetch('/api/shell', {method:'POST', body:'service network restart'})">Restart</button>
                        </div>
                    `;
                } else if (tab === 'security') {
                    fetch('/api/net/stats').then(r=>r.json()).then(stats => {
                        content.innerHTML = `
                            <h3>Network Monitor</h3>
                            <div class="stats-grid">
                                <div class="stat-box"><h4>RX Packets</h4><p>${stats.rx_packets}</p></div>
                                <div class="stat-box"><h4>TX Packets</h4><p>${stats.tx_packets}</p></div>
                                <div class="stat-box"><h4>RX Bytes</h4><p>${stats.rx_bytes}</p></div>
                                <div class="stat-box"><h4>TX Bytes</h4><p>${stats.tx_bytes}</p></div>
                                <div class="stat-box" style="color:#ef4444"><h4>Dropped</h4><p>${stats.rx_dropped}</p></div>
                            </div>
                            <br>
                            <a href="/api/net/pcap" class="editor-btn" style="text-decoration:none; padding:8px 12px; display:inline-block;">Download PCAP (.pcap)</a>
                        `;
                    }).catch(e => {
                        content.innerHTML = 'Error loading stats: ' + e;
                    });
                }
            };
            
            // Load default tab
            window.showControlTab('network', win.querySelector('.control-sidebar-item'));
        });
    }
}

// Global Open Editor helper
function openEditor(filePath = '') {
    const html = `
        <div class="editor-layout">
            <div class="editor-toolbar">
                <input type="text" class="editor-input" placeholder="/disk/fat0/file.txt" value="${filePath}">
                <button class="editor-btn" id="btn-load">Load</button>
                <button class="editor-btn" id="btn-save">Save</button>
            </div>
            <textarea class="editor-textarea" spellcheck="false" placeholder="Type here..."></textarea>
        </div>
    `;
    createWindow('Text Editor', html, (win) => {
        const pathInput = win.querySelector('.editor-input');
        const textarea = win.querySelector('.editor-textarea');
        const btnLoad = win.querySelector('#btn-load');
        const btnSave = win.querySelector('#btn-save');
        
        const loadFile = () => {
            const path = pathInput.value.trim();
            if(!path) return;
            btnLoad.innerText = "Loading...";
            fetch('/api/file' + path)
                .then(res => res.text())
                .then(text => {
                    if(text === 'File not found.') {
                        alert('File not found!');
                        textarea.value = '';
                    } else {
                        textarea.value = text;
                    }
                    btnLoad.innerText = "Load";
                })
                .catch(e => {
                    alert('Error: ' + e);
                    btnLoad.innerText = "Load";
                });
        };
        
        btnLoad.onclick = loadFile;
        
        btnSave.onclick = () => {
            const path = pathInput.value.trim();
            if(!path) return alert("Please enter a path!");
            btnSave.innerText = "Saving...";
            fetch('/api/file' + path, {
                method: 'POST',
                body: textarea.value
            })
            .then(res => res.text())
            .then(text => {
                btnSave.innerText = "Save";
            })
            .catch(e => {
                alert('Error: ' + e);
                btnSave.innerText = "Save";
            });
        };
        
        if (filePath) loadFile();
    });
}
