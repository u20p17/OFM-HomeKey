load('lib/OFM-HomeKey/src/HomeKey.script.js');
function assert(value, text) { if (!value) throw new Error(text); }
var tests = 0;
function test(name, run) { run(); tests++; print('PASS: ' + name); }
function fixture(entries) {
    var parameters = { HK_OnlineStatus: { value: '' } }, calls = [], disconnected = 0, generation = [0,0,0,1], opened = false;
    for (var i=1;i<=16;i++) {
        var p='HK_Authorization'+(i<10?'0':'')+i;
        parameters[p+'Enabled']={value:0}; parameters[p+'Issuer']={value:''}; parameters[p+'Endpoint']={value:''}; parameters[p+'AccessId']={value:0}; parameters[p+'Description']={value:''};
    }
    var f={ parameters:parameters, calls:calls, fail:null, canceled:false, programming:true, generation:generation, entries:entries,
        device:{getParameterByName:function(name){return parameters[name];}},
        online:{connect:function(){},disconnect:function(){disconnected++;}},
        progress:{setText:function(){},setProgress:function(){},isCanceled:function(){return f.canceled;}},
        disconnected:function(){return disconnected;}, opened:function(){return opened;}};
    f.backend=function(data) {
        calls.push(data.slice(0));
        if(f.fail) { var failed=f.fail(data); if(failed) return failed; }
        if(data[0]==0) return [0,1,1,1,opened?1:0,opened?1:0,2,f.entries.length].concat(f.generation);
        if(data[0]==1) {opened=true;return [0,1];}
        if(data[0]==2) {opened=false;return [0,1];}
        if(data[0]==8) {
            if(!f.programming)return [10,1];
            if(JSON.stringify(data.slice(1,5))!=JSON.stringify(f.generation))return [8,1];
            var identity=HK_hex(data,5,14);
            f.entries=f.entries.filter(function(e){return HK_hex(e.issuer.concat(e.endpoint),0,14)!=identity;});
            f.generation[3]++;opened=false;return [0,1];
        }
        var e=f.entries[data[1]];
        return [0,1,data[1],f.entries.length].concat(f.generation,e.issuer,e.endpoint);
    };
    BASE_invokeFunctionPropertyWrapper=function(object,property,data,device,online,progress) {
        assert(object==165&&property==3,'Wrong online function property'); return f.backend(data);
    };
    return f;
}
function entry(n) { return {issuer:[0,17,34,51,68,85,170,187],endpoint:[1,35,69,103,137,n]}; }
function readFields(f,row) {var p='HK_Authorization'+(row<10?'0':'')+row; return [f.parameters[p+'Enabled'].value,f.parameters[p+'Issuer'].value,f.parameters[p+'Endpoint'].value,f.parameters[p+'AccessId'].value];}
function rejects(run, text) {var failed=false;try{run();}catch(e){failed=true;}assert(failed,text);}
function imported(f){HK_importEndpoints(f.device,f.online,f.progress,{});}

test('Import full IDs with leading zeros; newly imported rows stay disabled',function(){
    var f=fixture([entry(171),entry(172)]); imported(f);
    assert(JSON.stringify(readFields(f,1))==JSON.stringify([0,'001122334455aabb','0123456789ab',0]),'Incorrect IDs/permission');
    assert(readFields(f,2)[2]=='0123456789ac','Second endpoint missing'); assert(f.disconnected()==1,'Connection leaked');
});
test('Preserve existing rights, namespaced case-insensitive deduplication and partially filled rows',function(){
    var f=fixture([entry(171),entry(172)]),p=f.parameters;
    p.HK_Authorization01Issuer.value='001122334455AABB';p.HK_Authorization01Endpoint.value='0123456789AB';p.HK_Authorization01AccessId.value=42;p.HK_Authorization01Enabled.value=1;
    p.HK_Authorization02Issuer.value='unfinished';
    imported(f); assert(readFields(f,1)[0]==1&&readFields(f,1)[3]==42,'Existing rights changed'); assert(readFields(f,2)[1]=='unfinished','Partial row overwritten');assert(readFields(f,3)[2]=='0123456789ac','New device not added');
});
test('A full table rejects before any change',function(){var f=fixture([entry(171)]);for(var i=1;i<=16;i++){var p='HK_Authorization'+(i<10?'0':'')+i;f.parameters[p+'Issuer'].value='occupied';}var before=JSON.stringify(f.parameters);rejects(function(){imported(f);},'Should reject capacity');for(var i=1;i<=16;i++)assert(readFields(f,i)[1]=='occupied','Existing row changed');assert(f.disconnected()==1,'Connection leaked');});
test('Malformed or inconsistent replies never update the table',function(){var f=fixture([entry(171)]);f.fail=function(data){if(data[0]==3)return [0,1,0,1,0,0,0,99].concat(entry(171).issuer,entry(171).endpoint);};rejects(function(){imported(f);},'Should reject generation mismatch');assert(readFields(f,1)[1]=='','Partial import');});
test('Snapshot changes retry and exhaustion leaves parameters untouched',function(){var f=fixture([entry(171)]);f.fail=function(data){if(data[0]==3)return [8,1];};var original=HK_wait;HK_wait=function(){};try{rejects(function(){imported(f);},'Should reject stale snapshot');}finally{HK_wait=original;}assert(readFields(f,1)[1]=='','Partial import');assert(f.calls.filter(function(c){return c[0]==3;}).length==3,'Retries are not bounded');});
test('A fresh snapshot succeeds after a stale response',function(){var f=fixture([entry(171)]),first=true;f.fail=function(data){if(data[0]==3&&first){first=false;f.generation=[0,0,0,2];return [8,1];}};var original=HK_wait;HK_wait=function(){};try{imported(f);}finally{HK_wait=original;}assert(readFields(f,1)[2]=='0123456789ab','Retry failed');});
test('Cancel reading leaves the table unchanged and closes the connection',function(){var f=fixture([entry(171)]);f.canceled=true;rejects(function(){imported(f);},'Should cancel');assert(readFields(f,1)[1]=='','Canceled import changed rows');assert(f.disconnected()==1,'Connection leaked');});
test('Missing fields and duplicate device IDs do not produce partial imports',function(){var f=fixture([entry(171)]);delete f.parameters.HK_Authorization16AccessId;rejects(function(){imported(f);},'Should reject wrong application');assert(readFields(f,1)[1]=='','Partial import');f=fixture([entry(171),entry(171)]);rejects(function(){imported(f);},'Should reject duplicates');assert(readFields(f,1)[1]=='','Partial import');});
test('Failed parameter writes roll back newly touched rows',function(){var f=fixture([entry(171),entry(172)]),value='',once=true;Object.defineProperty(f.parameters.HK_Authorization02Endpoint,'value',{get:function(){return value;},set:function(v){if(once&&v){once=false;throw new Error('setter failed');}value=v;}});rejects(function(){imported(f);},'Should reject setter');assert(readFields(f,1)[1]==''&&readFields(f,2)[1]=='','Rollback failed');});
test('Pairing open/close buttons and status refresh use online management',function(){var f=fixture([]);HK_openPairing(f.device,f.online,f.progress,{});assert(f.opened(),'Open failed');HK_closePairing(f.device,f.online,f.progress,{});assert(!f.opened(),'Close failed');HK_refreshStatus(f.device,f.online,f.progress,{});assert(f.parameters.HK_OnlineStatus.value.indexOf('geschlossen')>=0,'Status missing');assert(f.disconnected()==3,'Connection leaked');});
test('Cancellation after an accepted open closes the queued request',function(){var f=fixture([]);f.fail=function(data){if(data[0]==1)f.canceled=true;};rejects(function(){HK_openPairing(f.device,f.online,f.progress,{});},'Should cancel');assert(!f.opened()&&f.calls.some(function(c){return c[0]==2;}),'Late open not canceled');});
test('Worker provisioning failures are surfaced and cancel the accepted open',function(){var f=fixture([]),requested=false;f.fail=function(data){if(data[0]==1)requested=true;if(data[0]==0&&requested)return [0,1,1,1,0,4,2,0,0,0,0,1];};rejects(function(){HK_openPairing(f.device,f.online,f.progress,{});},'Should reject no private code');assert(!f.opened(),'Window left open');});
test('Descriptions survive import; a named incomplete row is not reused',function(){
    var f=fixture([entry(171)]);f.parameters.HK_Authorization01Description.value='Julias iPhone';
    imported(f);assert(f.parameters.HK_Authorization01Description.value=='Julias iPhone','Description overwritten');
    assert(readFields(f,1)[1]==''&&readFields(f,2)[2]=='0123456789ab','Named row reused');
});
test('Delete selected device, all matching ETS rows and descriptions; keep other devices',function(){
    var f=fixture([entry(171),entry(172)]);imported(f);
    f.parameters.HK_Authorization01Description.value='Altes iPhone';
    f.parameters.HK_Authorization02Description.value='Neues iPhone';
    f.parameters.HK_Authorization03Issuer.value=readFields(f,1)[1];f.parameters.HK_Authorization03Endpoint.value=readFields(f,1)[2];
    f.parameters.HK_Authorization03Description.value='Duplikat';
    HK_deleteAuthorization01(f.device,f.online,f.progress,{});
    assert(f.entries.length==1&&f.entries[0].endpoint[5]==172,'Wrong device deleted');
    assert(readFields(f,1)[1]==''&&readFields(f,3)[1]=='','Matching rows not cleared');
    assert(f.parameters.HK_Authorization01Description.value==''&&f.parameters.HK_Authorization03Description.value=='','Description not cleared');
    assert(f.parameters.HK_Authorization02Description.value=='Neues iPhone'&&readFields(f,2)[2]=='0123456789ac','Other row changed');
    var request=f.calls.filter(function(c){return c[0]==8;})[0];assert(request.length==19&&request[1]==0&&request[4]==1,'Deletion did not use generation snapshot');
});
test('Already absent device can be removed from ETS without deleting a different endpoint',function(){
    var f=fixture([entry(171)]);imported(f);f.entries=[];
    HK_deleteAuthorization01(f.device,f.online,f.progress,{});
    assert(readFields(f,1)[1]=='','Stale ETS row retained');assert(f.calls.some(function(c){return c[0]==8;}),'Absent identity was not persistently revoked');
});
test('No programming mode, stale list or device write failure leaves ETS rights intact',function(){
    for(var failure=0;failure<3;failure++){
        var f=fixture([entry(171)]);imported(f);f.parameters.HK_Authorization01Description.value='Keep me';
        if(failure==0)f.programming=false;
        else f.fail=function(data){if(data[0]==8)return [failure==1?8:7,1];};
        rejects(function(){HK_deleteAuthorization01(f.device,f.online,f.progress,{});},'Deletion should fail');
        assert(readFields(f,1)[2]=='0123456789ab'&&f.parameters.HK_Authorization01Description.value=='Keep me','Failed deletion cleared ETS');
        assert(f.entries.length==1,'Failed deletion changed devices');
    }
});
test('Canceled deletion never invokes mutating command',function(){
    var f=fixture([entry(171)]);imported(f);f.canceled=true;
    rejects(function(){HK_deleteAuthorization01(f.device,f.online,f.progress,{});},'Deletion should cancel');
    assert(!f.calls.some(function(c){return c[0]==8;})&&readFields(f,1)[1]!='','Cancellation mutated credentials');
});
test('Real Common wrapper reassembles endpoint replies at 15-byte APDU size',function(){
    var f=fixture([entry(171)]),incoming=[],reply=[],position=0,packages=0,reads=0;
    Log={info:function(){}};
    load('lib/OGM-Common/src/Common.script.js');
    f.online.getMaxApduLength=function(){return 15;};
    f.online.invokeFunctionProperty=function(object,property,data){
        assert(object==158&&property==3,'Wrong wrapper transport');
        if(data[0]==0){incoming=[];packages=data[2]*256+data[3];return [0,2];}
        if(data[0]==1){assert(data[1]==165&&data[2]==3,'Wrong wrapped target');reply=f.backend(incoming);position=0;return [0,0,reply.length,254];}
        if(data[0]<128){incoming=incoming.concat(data.slice(1));return [0,--packages?data[0]+1:254];}
        if(position>=reply.length)return [0];
        var chunk=reply.slice(position,position+14);position+=chunk.length;reads++;return [data[0]].concat(chunk);
    };
    imported(f);assert(readFields(f,1)[2]=='0123456789ab','Fragmented endpoint lost');assert(reads==4,'Expected two endpoint fragments and two status replies');
});
print(tests+' Home Key ETS JavaScript tests passed');
