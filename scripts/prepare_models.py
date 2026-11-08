"""Offline-only conversion and official-reference parity. Never imported by the addon."""
import argparse, hashlib, json, os, sys, types
from pathlib import Path
os.environ['CUDA_VISIBLE_DEVICES'] = '-1'
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'
os.environ['TF_ENABLE_ONEDNN_OPTS'] = '0'
import numpy as np
import cv2
import onnx
import onnxruntime as ort
import tensorflow as tf
import tf2onnx

ROOT = Path(__file__).resolve().parents[1]
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def module_from_source(name, path):
    source=path.read_text().replace('import tensorflow as tf','import tensorflow.compat.v1 as tf').replace('import tensorflow.contrib.slim as slim','import tf_slim as slim')
    module=types.ModuleType(name);exec(compile(source,str(path),'exec'),module.__dict__);return module

def main():
    ap=argparse.ArgumentParser();ap.add_argument('model',choices=['animegan','whitebox']);ap.add_argument('--width',type=int,default=1280);ap.add_argument('--height',type=int,default=720);ap.add_argument('--image',type=Path,required=True);a=ap.parse_args()
    if a.width%8 or a.height%8: raise ValueError('Dimensions must be multiples of eight')
    tf.compat.v1.disable_eager_execution()
    folder=ROOT/'models';folder.mkdir(exist_ok=True);out=folder/f'{a.model}-{a.width}x{a.height}.onnx'
    ev=ROOT/'evaluation';ev.mkdir(exist_ok=True)
    src=cv2.imread(str(a.image));src=cv2.resize(src,(a.width,a.height),interpolation=cv2.INTER_LINEAR)
    rgb=a.model=='animegan';image=cv2.cvtColor(src,cv2.COLOR_BGR2RGB) if rgb else src
    x=image.astype(np.float32)[None]/127.5-1
    graph=tf.Graph()
    with graph.as_default():
        if rgb:
            upstream=ROOT/'external/AnimeGANv2/pb_and_onnx_model'
            gd=tf.compat.v1.GraphDef();gd.ParseFromString((upstream/'Shinkai_53.pb').read_bytes())
            tf.import_graph_def(gd,name='')
            input_tensor=graph.get_tensor_by_name('generator_input:0');output_tensor=graph.get_tensor_by_name('generator/G_MODEL/out_layer/Tanh:0')
            frozen=gd;weight=upstream/'Shinkai_53.pb';revision='724b5ce75185c2e838d30c9077dbb967ab034254'
        else:
            upstream=ROOT/'external/White-box-Cartoonization/test_code'
            network=module_from_source('official_network',upstream/'network.py');guided=module_from_source('official_guided',upstream/'guided_filter.py')
            input_tensor=tf.compat.v1.placeholder(tf.float32,[1,a.height,a.width,3],name='cel_input')
            generated=network.unet_generator(input_tensor)
            output_tensor=tf.identity(guided.guided_filter(input_tensor,generated,r=1,eps=5e-3),name='cel_output')
            weight=upstream/'saved_models/model-33999.data-00000-of-00001';revision='ee97277d7fd873b89e9b8e1341efe74e10dd4124'
        with tf.compat.v1.Session(graph=graph) as sess:
            if not rgb:
                sess.run(tf.compat.v1.global_variables_initializer());tf.compat.v1.train.Saver().restore(sess,str(upstream/'saved_models/model-33999'))
                frozen=tf.compat.v1.graph_util.convert_variables_to_constants(sess,graph.as_graph_def(),[output_tensor.op.name])
            reference=sess.run(output_tensor,{input_tensor:x})
    converted,_=tf2onnx.convert.from_graph_def(frozen,input_names=[input_tensor.name],output_names=[output_tensor.name],
        shape_override={input_tensor.name:[1,a.height,a.width,3]},opset=17,output_path=str(out))
    session=ort.InferenceSession(str(out),providers=['CPUExecutionProvider'])
    actual=session.run(None,{session.get_inputs()[0].name:x})[0]
    error=np.abs(actual-reference);metrics={'max_abs':float(error.max()),'mean_abs':float(error.mean())}
    # In normalized [-1,1] units: <=1.275 channel levels worst-case, <=0.255 mean.
    # Recorded after observing a 0.329-level max difference in the official Shinkai graph conversion.
    metrics['acceptance']={'max_abs':0.01,'mean_abs':0.002}
    if metrics['max_abs']>.01 or metrics['mean_abs']>.002:raise RuntimeError(f'Export parity failed: {metrics}')
    image=np.clip((actual[0]+1)*127.5,0,255).astype(np.uint8)
    if rgb:image=cv2.cvtColor(image,cv2.COLOR_RGB2BGR)
    cv2.imwrite(str(ev/f'{a.model}-{a.width}x{a.height}-reference.png'),image)
    np.save(ev/f'{a.model}-{a.width}x{a.height}-reference.npy',reference)
    manifest={'schema':1,'name':a.model,'upstream':'https://github.com/'+('TachibanaYoshino/AnimeGANv2' if rgb else 'SystemErrorWang/White-box-Cartoonization'),
        'revision':revision,'weight_filename':weight.name,'weight_sha256':sha(weight),'onnx_sha256':sha(out),
        'input':session.get_inputs()[0].name,'output':session.get_outputs()[0].name,'shape':[1,a.height,a.width,3],'layout':'NHWC','dtype':'float32','color':'RGB' if rgb else 'BGR',
        'normalization':'display-encoded channel / 127.5 - 1','postprocessing':'(output + 1) * 127.5, clip [0,255]',
        'official_pipeline':'Official frozen Shinkai_53 generator, optional brightness adjustment disabled (supported by official test.py)' if rgb else 'Official generator plus guided filter r=1 eps=0.005; TF1 APIs mapped to compat.v1 and tf_slim without changing operations or weights',
        'runtime_resize':'Aspect-preserving bilinear fit with edge padding; inverse fit on output. Evaluation source already resized to exact dimensions. Runtime resize is an explicit deployment policy, distinct from upstream resize/crop.',
        'license_evidence':'external/AnimeGANv2/README.md' if rgb else 'external/White-box-Cartoonization/README.md',
        'redistribution':'Unreviewed; local evaluation only; no weights or engines approved for public distribution','parity':metrics,'evaluation_image_sha256':sha(a.image)}
    out.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
