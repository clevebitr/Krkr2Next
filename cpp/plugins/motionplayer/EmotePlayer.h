//
// Created by LiDon on 2025/9/15.
//
// Motion.EmotePlayer —— E-mote 播放器壳层。
//
// 背景（NEKOPARA 4 真机实证）：Windows 原版里 EmotePlayer 由 emoteplayer.dll
// 注册，带整套 emote API；本仓库早年只留了一个空壳（仅有 useD3D）。走
// Motion.EmotePlayer 的游戏（NEKOPARA 4 的 system/AffineSourceMotion.tjs）在
// createPlayer 第 63 字节即抛 `Member "MaskModeAlpha" does not exist`，动态立绘
// 创建失败；随后 removePlayer 又抛 `Member "skip" does not exist`，读档流程被这
// 两个异常打断（游戏自写的 savedata/krkr.console.log 实证）。
//
// 现在：壳层持有一个本仓库已实现好的 motion::Player；main.cpp 用同一份宏把
// Player 的整套成员注册到 Motion.EmotePlayer 上（GetPlayerInstance 会把壳层里的
// Player 交出来），再补 emote 专有成员。凡本引擎没有对应实现的 emote 专有成员
// （Timeline 差分表情、物理风、序列化……）一律给"接受参数、返回空值"的宽容实现：
// 少一层差分表情可以接受，缺成员抛异常会直接毁掉整个播放器与读档流程。
#pragma once

#include "Player.h"
#include "ResourceManager.h"

namespace motion {

    // 与 Windows 原版 D3DEmoteModule/EmotePlayer 一致的遮罩模式值。
    enum MaskMode { MaskModeStencil = 0, MaskModeAlpha = 1 };

    class EmotePlayer {
    public:
        // 原版构造收 (ResourceManager)：本引擎的 Player 自己按存储路径取 PSB，
        // 不需要壳层持有 manager，因此参数按兼容签名收下即丢。
        explicit EmotePlayer(ResourceManager) {}

        Player &player() { return _player; }
        const Player &player() const { return _player; }

        // ── emote 壳层自有状态（本引擎只存储，不参与渲染）──
        [[nodiscard]] tjs_int getMaskMode() const { return _maskMode; }
        void setMaskMode(tjs_int v) { _maskMode = v; }

        [[nodiscard]] double getHairScale() const { return _hairScale; }
        void setHairScale(double v) { _hairScale = v; }

        [[nodiscard]] double getPartsScale() const { return _partsScale; }
        void setPartsScale(double v) { _partsScale = v; }

        [[nodiscard]] double getBustScale() const { return _bustScale; }
        void setBustScale(double v) { _bustScale = v; }

        [[nodiscard]] double getBodyScale() const { return _bodyScale; }
        void setBodyScale(double v) { _bodyScale = v; }

        [[nodiscard]] bool getVisible() const { return _visible; }
        void setVisible(bool v) { _visible = v; }

        [[nodiscard]] bool getSmoothing() const { return _smoothing; }
        void setSmoothing(bool v) { _smoothing = v; }

        [[nodiscard]] bool getQueing() const { return _queing; }
        void setQueing(bool v) { _queing = v; }

        [[nodiscard]] ttstr getMotionKey() const { return _motionKey; }
        void setMotionKey(ttstr v) { _motionKey = v; }

    private:
        Player _player;
        tjs_int _maskMode = MaskModeAlpha;
        double _hairScale = 1.0;
        double _partsScale = 1.0;
        double _bustScale = 1.0;
        double _bodyScale = 1.0;
        bool _visible = true;
        bool _smoothing = false;
        bool _queing = false;
        ttstr _motionKey;
    };

} // namespace motion
