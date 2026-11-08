"""Compare native D3D11/CUDA/TRT output to ONNX CPU, including color ordering and PNG encoding."""
import argparse,json,subprocess
from pathlib import Path
import cv2,numpy as np,onnxruntime as ort
p=argparse.ArgumentParser();p.add_argument('model',type=Path);p.add_argument('engine',type=Path);p.add_argument('--source',type=Path,default=Path('docs/images/original.png'));p.add_argument('--backend',default='dist/local');a=p.parse_args()
meta=json.loads(a.model.with_suffix('.json').read_text());_,h,w,_=meta['shape']
source=cv2.resize(cv2.imread(str(a.source)),(w,h),interpolation=cv2.INTER_LINEAR)
ev=Path('evaluation');ev.mkdir(exist_ok=True);inp=ev/f'{a.model.stem}-validation-input.png';out=ev/f'{a.engine.stem}-validation.png';cv2.imwrite(str(inp),source)
r=subprocess.run(['build/cel_benchmark.exe',str(inp),str(out),str(a.engine),str(w),str(h),meta['color'].lower(),'30',a.backend],capture_output=True,text=True)
if r.returncode:raise RuntimeError(r.stdout+r.stderr)
x=source if meta['color']=='BGR' else cv2.cvtColor(source,cv2.COLOR_BGR2RGB)
s=ort.InferenceSession(str(a.model),providers=['CPUExecutionProvider']);y=s.run(None,{meta['input']:x.astype(np.float32)[None]/127.5-1})[0][0]
expected=np.clip((y+1)*127.5,0,255)
if meta['color']=='RGB':expected=expected[:,:,::-1]
actual=cv2.imread(str(out)).astype(np.float32);error=np.abs(actual-expected)
report={'model':meta['name'],'shape':meta['shape'],'engine':a.engine.name,'max_channel_error':float(error.max()),'mean_channel_error':float(error.mean()),'p99_channel_error':float(np.percentile(error,99)),
        'acceptance':{'max_channel_error':10,'mean_channel_error':1},'includes':'native preprocessing, model inference, postprocessing, channel order, PNG output'}
(ev/f'{a.engine.stem}-parity.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
if report['mean_channel_error']>1 or report['max_channel_error']>10:raise RuntimeError('Native GPU vs ONNX parity failed')
