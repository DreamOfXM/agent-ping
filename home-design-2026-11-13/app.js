const $=s=>document.querySelector(s);
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const icon=(name,cls='')=>`<svg class="${cls}" aria-hidden="true"><use href="#${name}"/></svg>`;
const tools={claude:{name:'Claude Code',short:'Claude',asset:'claude.svg',client:'CLI'},qoder:{name:'Qoder',short:'Qoder',asset:'qoder.png',client:'IDE'},zcode:{name:'ZCode',short:'ZCode',asset:'zcode.png'},codex:{name:'Codex',short:'Codex'},workbuddy:{name:'WorkBuddy',short:'WorkBuddy',asset:'workbuddy.png'},opencode:{name:'OpenCode',short:'OpenCode',asset:'opencode.png'}};
const mark=id=>tools[id].asset?`<img src="assets/${tools[id].asset}" alt="">`:icon('terminal');
const history=[
{id:'h1',tool:'qoder',project:'budget-app',time:'22:57',turns:331,unread:true},
{id:'h2',tool:'claude',project:'kitchen-timer',time:'21:17',turns:16,unread:true},
{id:'h3',tool:'qoder',project:'image-tools',time:'19:03',turns:28,unread:true},
{id:'h4',tool:'claude',project:'poetry-site',time:'16:48',turns:18},
{id:'h5',tool:'claude',project:'photo-sorter',time:'14:22',turns:196},
{id:'h6',tool:'qoder',project:'budget-app',time:'12:05',turns:31},
{id:'h9',tool:'codex',project:'kitchen-timer',time:'11:08',turns:9},
{id:'h7',tool:'claude',project:'habit-box',time:'10:40',turns:130},
{id:'h8',tool:'qoder',project:'image-tools',time:'08:12',turns:17}
].map(d=>({...d,kind:'review',age:d.time,request:`会话已完成 · ${d.turns} 轮`,usage:d.tool==='claude'?`${((18000+d.turns*137)/1000).toFixed(1)}K tokens`:d.tool==='qoder'?`${(12.5+(d.turns%60)*1.7).toFixed(1)} Credits`:''}));
const waiting=[
{id:'e5',tool:'claude',project:'photo-sorter',kind:'permission',age:'13h',request:'write ~/…/app-icon.svg',usage:'18K tokens',detail:'请求写入工作区以外的文件。'},
{id:'d4',tool:'qoder',project:'budget-app',kind:'permission',age:'9h',request:'cd /Users/you/projects/mobile-app',usage:'12.5 Credits',detail:'请求执行 Bash 命令。'},
{id:'c3',tool:'qoder',project:'image-tools',kind:'permission',age:'1h 07m',request:"rm '/tmp_cache.txt'",usage:'12.5 Credits',detail:'此命令会删除文件，需要在原应用中确认。'},
{id:'b2',tool:'qoder',project:'image-tools',kind:'question',age:'15h',request:'这个模块的旧版本还挂在别处，怎么处理？',options:['先搬再删','只留新的','两个都停'],usage:'12.5 Credits'},
{id:'a1',tool:'qoder',project:'budget-app',kind:'question',age:'49m',request:'这批改动要一起提交吗？还是先拆成两笔？',options:['一起提交','拆成两笔','先不提交'],usage:'12.5 Credits'}
];
const live={id:'live',tool:'qoder',project:'image-tools',kind:'running',age:'刚刚',title:'多 Agent 监控工具调研',request:'rg -n "safe-delete" src/',usage:'41.4 Credits',turns:77,duration:'4h 12m',loop:6,children:[{text:'收集三款同类产品的定价页与收费方式。',running:true},{text:'只读设置屏，列出可借鉴的三处。'},{text:'把结论合成一张对比表。'}]};
const extraRunning=[
{id:'sample-claude',tool:'claude',project:'photo-sorter',title:'整理照片目录与重复文件',request:'核对重复文件的判定规则。'},
{id:'sample-codex',tool:'codex',project:'kitchen-timer',title:'调整计时器的提醒交互',request:'检查提醒的关闭与返回操作。'},
{id:'sample-workbuddy',tool:'workbuddy',project:'budget-app',title:'检查预算页面的交互状态',request:'检查预算表单的输入与保存流程。'},
{id:'sample-opencode',tool:'opencode',project:'poetry-site',title:'调整诗歌列表的阅读样式',request:'核对长标题换行与窄屏布局。'},
{id:'sample-zcode',tool:'zcode',project:'habit-box',title:'验证习惯打卡的空状态',request:'检查没有打卡记录时的页面。'}
].map(t=>({...t,kind:'running',sample:true}));
const labels={permission:'待授权',question:'待回答',review:'待查看',running:'运行中'};
let source='all',scenario=3,showAll=false,query='',expanded=new Set(),viewed=new Set(),sheetInvoker=null,toastTimer;
const all=()=>[live,...extraRunning.slice(0,scenario-1),...waiting,...history];
const matches=t=>(source==='all'||t.tool===source)&&(!query||[t.project,t.title||'',t.request,tools[t.tool].name].join(' ').toLowerCase().includes(query));
const running=()=>all().filter(t=>t.kind==='running'&&matches(t));
const pending=()=>[...waiting,...history.filter(t=>t.unread&&!viewed.has(t.id))].filter(matches);
const complete=()=>history.filter(matches);
const title=t=>t.title||t.project;
function restoreFocus(selector){$(selector)?.focus({preventScroll:true});}
function renderTools(){
 const fourth=['all','claude','qoder','zcode'].includes(source)?'codex':source;
 $('#tools').innerHTML=`<button class="tool" data-source="all" aria-pressed="${source==='all'}">${icon('layers')}<span>全部</span></button>`+['claude','qoder','zcode',fourth].map(id=>`<button class="tool" data-source="${id}" aria-pressed="${source===id}">${mark(id)}<span>${tools[id].short}</span></button>`).join('')+`<button class="tool more" id="all-tools" aria-label="全部六款工具">${icon('grid')}</button>`;
}
function detail(t){
 if(t.kind==='running')return `<div class="command">${icon('terminal')}<code>${esc(t.request)}</code></div>${t.turns?`<div class="detail-stats"><span><strong>${t.turns}</strong> 轮</span><span>已运行 <strong>${t.duration}</strong></span><span>循环 ×${t.loop}</span></div>`:''}${t.usage?`<p>会话用量 <strong>${esc(t.usage)}</strong></p>`:''}${t.children?`<div class="children-title">${icon('branch')}${t.children.length} 条子对话</div>${t.children.map(c=>`<div class="child">${c.running?'<i class="status-dot"></i>':icon('check')}<span>${esc(c.text)}</span></div>`).join('')}`:''}${t.sample?'<p class="sample-note">扩展交互样例，不代表真实运行状态；未设置轮次、时长或用量。</p>':''}`;
 return `${t.kind==='permission'?`<p>${esc(t.detail)}</p><div class="command">${icon('terminal')}<code>${esc(t.request)}</code></div>`:t.kind==='question'?`<p>${esc(t.request)}</p><div class="question-options">${t.options.map(s=>`<span>${esc(s)}</span>`).join('')}</div>`:`<p>${esc(t.request)} · ${t.time}</p>`}${t.usage?`<p>会话用量 <strong>${esc(t.usage)}</strong></p>`:''}${['permission','question'].includes(t.kind)?'<p class="sample-note">授权与回答在原应用中完成。</p>':''}`;
}
function conversation(t){
 const isRunning=t.kind==='running',isExpanded=expanded.has(t.id);
 const body=isRunning?`<span class="identity"><b>${tools[t.tool].name}</b>${tools[t.tool].client?`<span class="client-tag">${tools[t.tool].client}</span>`:''}<span class="project">${esc(t.project)}</span></span><strong class="session-title">${esc(title(t))}</strong>${t.turns?`<span class="activity"><span>${t.turns} 轮</span><span>${t.duration}</span><span>${t.children.length} 条子对话</span></span>`:t.sample?'<span class="activity">测试会话</span>':''}`:`<strong class="session-title">${esc(title(t))}</strong><span class="request-line">${esc(t.request)}</span>`;
 return `<article class="conversation ${isRunning?'running-card':'pending-card'}" data-id="${t.id}"><div class="row-actions"><button class="open-conversation" data-open="${t.id}" aria-label="打开 ${tools[t.tool].name} 会话：${esc(title(t))}" title="打开会话 · ${esc(title(t))}"><span class="avatar">${mark(t.tool)}</span><span class="row-copy">${body}</span>${isRunning?icon('arrow','launch-mark'):`<span class="pending-meta ${t.kind==='review'?'blue':''}">${labels[t.kind]}<small>${t.age}</small></span>`}</button><div class="action-rail"><button class="details-toggle" data-details="${t.id}" aria-label="${isExpanded?'收起':'展开'}详情：${esc(title(t))}" aria-expanded="${isExpanded}" aria-controls="details-${t.id}" title="${isExpanded?'收起':'展开'}详情">${icon('chevron')}</button></div></div><div class="details" id="details-${t.id}" ${isExpanded?'':'hidden'}>${detail(t)}</div></article>`;
}
function renderSessions(){
 const liveTasks=running(),waitingTasks=pending();
 if(!liveTasks.length&&!waitingTasks.length){$('#sessions').innerHTML=`<div class="empty">${icon(query?'search':'check')}<h2>${query?'没有匹配的会话':'暂无进行中的会话'}</h2><p>${query?'试试其他项目名，或清空搜索。':complete().length?'已完成的会话可从下方「今日完成」打开。':'当前工具的用量与额度在下方展示。'}</p>${query?'<button id="clear-query">清空搜索</button>':''}</div>`;return;}
 $('#sessions').innerHTML=`${liveTasks.length?`<section class="running-section" aria-labelledby="running-title"><header class="section-heading"><h2 id="running-title"><i class="status-dot"></i>运行中 <span class="count" id="running-count">${liveTasks.length}</span></h2><span class="hint">点击会话直接打开</span></header><div class="running-list">${liveTasks.map(conversation).join('')}</div></section>`:''}${waitingTasks.length?`<section class="pending-section" aria-labelledby="pending-title"><header class="section-heading"><h2 id="pending-title">待处理 <span class="count" id="pending-count">${waitingTasks.length}</span></h2><span class="hint">授权 · 回答 · 查看</span></header><div class="pending-list">${(showAll?waitingTasks:waitingTasks.slice(0,3)).map(conversation).join('')}${waitingTasks.length>3?`<button class="more-pending" id="more-pending" aria-expanded="${showAll}">${showAll?'收起待处理':`展开其余 ${waitingTasks.length-3} 项`}${icon('chevron')}</button>`:''}</div></section>`:''}`;
}
function gauge(tool,name,description,percent,extra=''){
 return `<button class="gauge-button ${extra}" data-quota="${tool}" aria-label="${tools[tool].name} ${name}剩余 ${percent}%"><span class="gauge-info"><span class="gauge-brand">${mark(tool)}${tools[tool].short}</span><small>${name}剩余</small><small${extra.includes('stale')?' class="stale-note"':''}>${description}</small></span><span class="gauge"><svg viewBox="0 0 64 64" aria-hidden="true"><circle class="track" cx="32" cy="32" r="27" pathLength="100" stroke-dasharray="75 100" transform="rotate(135 32 32)"/><circle class="value" cx="32" cy="32" r="27" pathLength="100" stroke-dasharray="${percent*.75} 100" transform="rotate(135 32 32)"/></svg><strong>${percent}<small>%</small></strong></span></button>`;
}
function renderQuotas(){
 let content='';
 if(source==='all')content=gauge('claude','5 小时','2h 16m 后重置',66)+gauge('zcode','账户额度','1d 18h 后到期',78);
 if(source==='claude')content=gauge('claude','5 小时','2h 16m 后重置',66)+gauge('claude','本周','3 天后重置',88);
 if(source==='zcode')content=gauge('zcode','账户额度','GLM-5.3-Flash · 1d 18h 后到期',78,'single');
 if(source==='workbuddy')content=gauge('workbuddy','5 小时','待更新 · 读数 22:35',29,'stale single');
 if(source==='qoder')content=`<button class="gauge-button single" data-quota="qoder"><span class="gauge-info"><span class="gauge-brand">${mark('qoder')}Qoder</span><small>账户 Credits · 已用尽</small><small class="stale-note">上次读数 19:05</small></span><span class="balance-number">0 <small>/ 100</small></span></button>`;
 if(source==='codex'||source==='opencode'){$('#quotas').innerHTML=`<div class="quota-heading"><span>今日用量</span><button data-page="usage">详情 ${icon('chevron')}</button></div><button class="quota-usage" data-page="usage">${tools[source].name}<span><strong>${source==='codex'?'132.22K':'51.2K'}</strong> <small>tokens</small></span></button>`;return;}
 $('#quotas').innerHTML=`<div class="quota-heading"><span>账户额度</span><button data-page="usage">详情 ${icon('chevron')}</button></div><div class="gauges">${content}</div>`;
}
function renderFooter(){
 $('#done-count').textContent=complete().length;
 $('#session-count').textContent=all().filter(matches).length;
 $('#timeline').innerHTML=complete().map(t=>{const [h,m]=t.time.split(':').map(Number);return `<i style="left:${(h*60+m-492)/(1415-492)*100}%;height:${5+Math.min(16,t.turns/22)}px"></i>`;}).join('');
}
function render(){renderTools();renderSessions();renderQuotas();renderFooter();}
function setSource(id){source=id;expanded.clear();showAll=false;render();$('#workspace').scrollTop=0;restoreFocus(`[data-source="${id}"]`);}
function toggleDetails(id){
 const top=$('#workspace').scrollTop;
 if(expanded.has(id))expanded.delete(id);else expanded.add(id);
 renderSessions();$('#workspace').scrollTop=top;restoreFocus(`[data-details="${id}"]`);
 const row=$(`[data-id="${id}"]`),view=$('#workspace').getBoundingClientRect(),box=row.getBoundingClientRect();
 if(box.bottom>view.bottom||box.top<view.top)row.scrollIntoView({block:box.height>view.height?'start':'nearest',behavior:'instant'});
}
function openSheet(title,html){
 if($('#sheet').hidden)sheetInvoker=document.activeElement;
 $('#sheet-title').textContent=title;$('#sheet-body').innerHTML=html;$('#sheet-body').scrollTop=0;$('#sheet').hidden=false;
 [...$('#app').children].filter(el=>el!==$('#sheet')&&el!==$('#toast')).forEach(el=>el.inert=true);$('#sheet-back').focus();
}
function closeSheet(){
 $('#sheet').hidden=true;[...$('#app').children].forEach(el=>el.inert=false);
 if(sheetInvoker?.isConnected)sheetInvoker.focus({preventScroll:true});else $('#session-tab').focus();
}
function toast(text){clearTimeout(toastTimer);$('#toast').textContent=text;$('#toast').hidden=false;toastTimer=setTimeout(()=>$('#toast').hidden=true,4000);}
function nativePage(name){openSheet(name==='usage'?'用量与额度':'权益',`<p class="sheet-note">当前原生版的全部工具实拍。此轮只改首页，内页以截图保留，图片不可操作。</p><img class="native-shot" src="assets/${name==='usage'?'overview-zh':'claims-zh'}.png" alt="${name==='usage'?'用量与额度':'权益'}原生截图">`);}
function openTask(id){
 const t=all().find(t=>t.id===id);if(!t)return;
 openSheet('会话交接预览',`<div class="handoff" data-handoff="${id}"><div class="handoff-icon"><span class="avatar">${mark(t.tool)}</span>${icon('arrow')}</div><span class="eyebrow">已触发打开会话 · 模拟</span><h3>${esc(title(t))}</h3><p class="target">${tools[t.tool].name} / ${esc(t.project)}</p><p class="sheet-note">正式 App 将直接定位到这条原会话，不需要先选中或展开详情。本页只模拟交接，不会打开真实会话，也不会执行授权、命令或回答。</p>${t.sample?'<p class="sheet-note">这是一条扩展测试会话，不代表真实运行状态。</p>':''}${t.kind==='review'&&t.unread&&!viewed.has(t.id)?`<button class="primary" data-viewed="${id}">标记已查看 ${icon('check')}</button>`:'<button class="primary" data-back>返回首页</button>'}</div>`);
}
function closeSearch(){query='';$('#search-input').value='';$('#search-wrap').hidden=true;$('#tools').hidden=false;$('#search-toggle').setAttribute('aria-expanded','false');render();$('#workspace').scrollTop=0;restoreFocus('#search-toggle');}
function reset(){
 if(!$('#sheet').hidden)closeSheet();source='all';query='';showAll=false;expanded.clear();viewed.clear();
 $('#search-input').value='';$('#search-wrap').hidden=true;$('#search-toggle').setAttribute('aria-expanded','false');$('#tools').hidden=false;$('#toast').hidden=true;clearTimeout(toastTimer);render();$('#workspace').scrollTop=0;
}
$('#app').addEventListener('click',e=>{
 const open=e.target.closest('[data-open]');if(open){openTask(open.dataset.open);return;}
 const details=e.target.closest('[data-details]');if(details){toggleDetails(details.dataset.details);return;}
 const sourceButton=e.target.closest('[data-source]');if(sourceButton){setSource(sourceButton.dataset.source);return;}
 const view=e.target.closest('[data-viewed]');if(view){viewed.add(view.dataset.viewed);closeSheet();renderSessions();restoreFocus('#session-tab');toast('已标记为查看，仅影响本次预览');return;}
 const tool=e.target.closest('[data-tool]');if(tool){closeSheet();setSource(tool.dataset.tool);return;}
 if(e.target.closest('[data-back]')){closeSheet();return;}
 if(e.target.closest('[data-page="usage"],[data-quota]')){nativePage('usage');return;}
 if(e.target.closest('#more-pending')){const top=$('#workspace').scrollTop;showAll=!showAll;renderSessions();$('#workspace').scrollTop=top;restoreFocus('#more-pending');return;}
 if(e.target.closest('#clear-query')){query='';$('#search-input').value='';render();$('#search-input').focus();return;}
 if(e.target.closest('#all-tools')){openSheet('全部工具',`<p class="sheet-note">选择工具后，会话与额度一起筛选。Codex 使用通用终端图标，不代表其官方标识。</p>${Object.entries(tools).map(([id,t])=>`<button class="sheet-tool" data-tool="${id}">${mark(id)}<span>${t.name}</span>${icon('chevron','chevron')}</button>`).join('')}`);return;}
});
$('#sheet-back').onclick=closeSheet;
$('#usage-tab').onclick=()=>nativePage('usage');
$('#claims-tab').onclick=()=>nativePage('claims');
$('#session-tab').onclick=()=>$('#workspace').scrollTo({top:0,behavior:'instant'});
$('#settings').onclick=()=>openSheet('设置','<p class="sheet-note">这是独立首页设计预览，保留设置入口；不会读取或改动正式 App 的通知、语言、侧边栏或其他设置。</p>');
$('#history').onclick=()=>openSheet('今日完成',`<p class="sheet-note">23:35 样例快照${source==='all'?'':` · ${tools[source].name}`}。点击任意记录直接触发会话交接。</p>${complete().length?complete().map(t=>`<button class="history-row" data-open="${t.id}">${mark(t.tool)}<span><strong>${esc(t.project)}</strong><small>${tools[t.tool].name} · ${t.turns} 轮</small></span><time>${t.time}</time></button>`).join(''):'<p class="sheet-note">没有匹配的完成记录。</p>'}`);
$('#search-toggle').onclick=()=>{if(!$('#search-wrap').hidden){closeSearch();return;}$('#tools').hidden=true;$('#search-wrap').hidden=false;$('#search-toggle').setAttribute('aria-expanded','true');$('#search-input').focus();};
$('#search-close').onclick=closeSearch;
$('#search-input').oninput=()=>{query=$('#search-input').value.trim().toLowerCase();showAll=!!query;expanded.clear();render();$('#workspace').scrollTop=0;};
$('#single').onclick=()=>{$('#stage').classList.remove('compare');$('#single').setAttribute('aria-pressed','true');$('#compare').setAttribute('aria-pressed','false');};
$('#compare').onclick=()=>{$('#stage').classList.add('compare');$('#single').setAttribute('aria-pressed','false');$('#compare').setAttribute('aria-pressed','true');};
$('#reset').onclick=reset;
document.querySelectorAll('[data-scenario]').forEach(button=>button.onclick=()=>{scenario=Number(button.dataset.scenario);reset();document.querySelectorAll('[data-scenario]').forEach(b=>b.setAttribute('aria-pressed',b===button));$('#preview-caption').textContent=`新首页 · ${scenario} 条运行`;});
$('#baseline').onclick=()=>$('#zoom').showModal();$('#zoom-close').onclick=()=>$('#zoom').close();
$('#sessions').addEventListener('keydown',e=>{if(!['ArrowDown','ArrowUp'].includes(e.key)||!e.target.matches('.open-conversation'))return;const buttons=[...$('#sessions').querySelectorAll('.open-conversation')],index=buttons.indexOf(e.target);buttons[(index+(e.key==='ArrowDown'?1:-1)+buttons.length)%buttons.length].focus();e.preventDefault();});
document.addEventListener('keydown',e=>{if(!$('#sheet').hidden){if(e.key==='Escape'){e.preventDefault();closeSheet();}if(e.key==='Tab'){const nodes=[...$('#sheet').querySelectorAll('button,a,input,[tabindex="0"]')].filter(n=>!n.disabled),first=nodes[0],last=nodes.at(-1);if(e.shiftKey&&document.activeElement===first){e.preventDefault();last.focus();}else if(!e.shiftKey&&document.activeElement===last){e.preventDefault();first.focus();}}}else if(e.key==='Escape'&&!$('#search-wrap').hidden)closeSearch();});
render();
