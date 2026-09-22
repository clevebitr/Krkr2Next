//
// Created by LiDon on 2025/9/15.
//
#pragma once

#include "tjs.h"

namespace motion {

    class SeparateLayerAdaptor {
    public:
        explicit SeparateLayerAdaptor(iTJSDispatch2 *owner = nullptr) :
            _owner(owner) {
            if(_owner) {
                _owner->AddRef();
            }
        }

        ~SeparateLayerAdaptor() {
            if(_target) {
                _target->Release();
                _target = nullptr;
            }
            if(_owner) {
                _owner->Release();
                _owner = nullptr;
            }
        }

        SeparateLayerAdaptor(const SeparateLayerAdaptor &) = delete;
        SeparateLayerAdaptor &operator=(const SeparateLayerAdaptor &) = delete;

        iTJSDispatch2 *getOwner() const { return _owner; }
        iTJSDispatch2 *getTarget() const { return _target; }
        void setTarget(iTJSDispatch2 *target) {
            if(target) {
                target->AddRef();
            }
            if(_target) {
                _target->Release();
            }
            _target = target;
        }

    private:
        iTJSDispatch2 *_owner = nullptr;
        iTJSDispatch2 *_target = nullptr;
    };
} // namespace motion

/** When \a base is a SeparateLayerAdaptor, returns its underlying Layer (with
 * layerTreeOwnerInterface). Otherwise returns \a base. Used so Layer
 * constructor receives an owner that has layerTreeOwnerInterface. */
iTJSDispatch2 *ResolveLayerTreeOwnerBase(iTJSDispatch2 *base);

/**
 * D3DAdaptor 的 "surface"：最近一次 captureCanvas(work) 传入的目标层。参考实现的
 * D3DAdaptor 有自己的渲染 surface；本壳没有，用它代替——它正是游戏随后
 * assignImages 到可见层的来源层。返回值由插件持有引用，调用方**不得** Release；
 * 未设置时返回 nullptr。
 */
iTJSDispatch2 *GetLastD3DAdaptorCaptureTarget();
