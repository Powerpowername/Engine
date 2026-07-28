#pragma once
#include "global.h"
#include <algorithm>
#include <cmath>

struct CameraCB
{
    DirectX::XMFLOAT4X4 view = {};
    DirectX::XMFLOAT4X4 projection = {};
    DirectX::XMFLOAT4X4 viewProjection = {};
    DirectX::XMFLOAT4 position = {};
};
class QuaternionCamera {
public:


    QuaternionCamera() {
        SetPerspective(DirectX::XM_PIDIV4, 16.0f / 9.0f, 0.1f, 1000.0f);
    }

    void SetPosition(const DirectX::XMFLOAT3& position) {
        this->position = position;
    }

    const DirectX::XMFLOAT3& GetPosition() const {
        return position;
    }

    void SetRotationQuaternion(const DirectX::XMFLOAT4& rotation) {
        DirectX::XMVECTOR q = DirectX::XMLoadFloat4(&rotation);
        if (DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(q)) < 1e-8f) {
            q = DirectX::XMQuaternionIdentity();
        } else {
            q = DirectX::XMQuaternionNormalize(q);
        }
        DirectX::XMStoreFloat4(&this->rotation, q);
    }

    const DirectX::XMFLOAT4& GetRotationQuaternion() const {
        return rotation;
    }

    void SetPerspective(float fovYRadians, float aspectRatio, float nearZ, float farZ) {
        this->fovYRadians = fovYRadians;
        this->aspectRatio = std::max(aspectRatio, 0.0001f);
        this->nearZ = nearZ;
        this->farZ = farZ;
    }

    void SetAspectRatio(float aspectRatio) {
        this->aspectRatio = std::max(aspectRatio, 0.0001f);
    }

    void SetOrientationCameraLookAt(
        const DirectX::XMFLOAT3& target,
        const DirectX::XMFLOAT3& worldUp = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f)) {
        using namespace DirectX;

        const XMVECTOR eye = XMLoadFloat3(&position);
        XMVECTOR forward = XMLoadFloat3(&target) - eye;

        if (XMVectorGetX(XMVector3LengthSq(forward)) < 1e-8f) {
            return;
        }

        forward = XMVector3Normalize(forward);

        XMVECTOR up = XMLoadFloat3(&worldUp);
        if (XMVectorGetX(XMVector3LengthSq(up)) < 1e-8f) {
            up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        }
        up = XMVector3Normalize(up);

        XMVECTOR right = XMVector3Cross(up, forward);
        if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-8f) {
            up = std::abs(XMVectorGetY(forward)) > 0.99f
                ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
                : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            right = XMVector3Cross(up, forward);
        }

        right = XMVector3Normalize(right);
        up = XMVector3Cross(forward, right);

        const XMMATRIX rotationMatrix(
            XMVectorGetX(right),   XMVectorGetY(right),   XMVectorGetZ(right),   0.0f,
            XMVectorGetX(up),      XMVectorGetY(up),      XMVectorGetZ(up),      0.0f,
            XMVectorGetX(forward), XMVectorGetY(forward), XMVectorGetZ(forward), 0.0f,
            0.0f,                  0.0f,                  0.0f,                  1.0f);

        StoreRotationFromMatrix(rotationMatrix);
    }

    void MoveWorld(const DirectX::XMFLOAT3& delta) {
        position.x += delta.x;
        position.y += delta.y;
        position.z += delta.z;
    }

    void MoveLocal(const DirectX::XMFLOAT3& delta) {
        using namespace DirectX;

        XMVECTOR localDelta = XMLoadFloat3(&delta);
        // 使用旋转四元数把局部坐标系的移动向量转换到世界坐标系。
        XMVECTOR worldDelta = XMVector3Rotate(localDelta, LoadRotation());

        XMFLOAT3 d = {};
        XMStoreFloat3(&d, worldDelta);

        position.x += d.x;
        position.y += d.y;
        position.z += d.z;
    }

    /// @brief 先yaw再pitch符合人的直观感觉
    /// @param yawRadians 为yaw角增量
    /// @param pitchRadians 为pitch角增量
    void AddYawPitch(float yawRadians, float pitchRadians) {
        if (yawRadians != 0.0f) {
            RotateWorldAxis(DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f), yawRadians);
        }

        if (pitchRadians != 0.0f) {
            RotateLocal(pitchRadians, 0.0f, 0.0f);
        }
    }

    void RotateLocal(float pitchRadians, float yawRadians, float rollRadians) {
        using namespace DirectX;

        const XMMATRIX current = XMMatrixRotationQuaternion(LoadRotation());
        const XMMATRIX delta = XMMatrixRotationRollPitchYaw(pitchRadians, yawRadians, rollRadians);

        StoreRotationFromMatrix(delta * current);
    }

    void RotateWorldAxis(const DirectX::XMFLOAT3& axis, float radians) {
        using namespace DirectX;
        // XMVECTOR是SIMD向量，他的优势是可以加速计算
        // SIMD 的意思是：CPU 一条指令同时处理多个 float。
        // SIMD 可以把 {x,y,z,w} 放进一个寄存器里批量算，所以 DirectXMath 很多函数都用 XMVECTOR 返回，而不是直接返回 float。
        XMVECTOR axisVector = XMLoadFloat3(&axis);
        if (XMVectorGetX(XMVector3LengthSq(axisVector)) < 1e-8f) {
            return;
        }

        axisVector = XMVector3Normalize(axisVector);

        const XMMATRIX current = XMMatrixRotationQuaternion(LoadRotation());
        const XMMATRIX delta = XMMatrixRotationAxis(axisVector, radians);

        StoreRotationFromMatrix(current * delta);
    }

    DirectX::XMMATRIX GetView() const {
        return DirectX::XMMatrixLookToLH(
            LoadPosition(),
            GetForwardVector(),
            GetUpVector());
    }

    DirectX::XMMATRIX GetProjection() const {
        return DirectX::XMMatrixPerspectiveFovLH(
            fovYRadians,
            aspectRatio,
            nearZ,
            farZ);
    }

    DirectX::XMMATRIX GetViewProjection() const {
        // DX的向量为行向量，因此矩阵乘法为行向量右乘矩阵
        return GetView() * GetProjection();
    }

    DirectX::XMFLOAT3 GetRight() const {
        return StoreVector3(GetRightVector());
    }

    DirectX::XMFLOAT3 GetUp() const {
        return StoreVector3(GetUpVector());
    }

    DirectX::XMFLOAT3 GetForward() const {
        return StoreVector3(GetForwardVector());
    }

    CameraCB GetCameraCB(bool transposeMatricesForHlsl = true) const {
        using namespace DirectX;

        CameraCB cb = {};

        XMMATRIX view = GetView();
        XMMATRIX projection = GetProjection();
        XMMATRIX viewProjection = view * projection;

        if (transposeMatricesForHlsl) {
            view = XMMatrixTranspose(view);
            projection = XMMatrixTranspose(projection);
            viewProjection = XMMatrixTranspose(viewProjection);
        }

        XMStoreFloat4x4(&cb.view, view);
        XMStoreFloat4x4(&cb.projection, projection);
        XMStoreFloat4x4(&cb.viewProjection, viewProjection);
        cb.position = XMFLOAT4(position.x, position.y, position.z, 1.0f);

        return cb;
    }

    float GetNearZ() const {
        return nearZ;
    }
    
    float GetFarZ() const {
        return farZ;
    }
    float GetFovYRadians() {return fovYRadians;}
    float GetAspectRatio() {return aspectRatio;}
private:
    DirectX::XMVECTOR LoadPosition() const {
        return DirectX::XMLoadFloat3(&position);
    }

    DirectX::XMVECTOR LoadRotation() const {
        return DirectX::XMLoadFloat4(&rotation);
    }

    DirectX::XMVECTOR GetRightVector() const {
        return DirectX::XMVector3Rotate(
            DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
            LoadRotation());
    }

    DirectX::XMVECTOR GetUpVector() const {
        return DirectX::XMVector3Rotate(
            DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
            LoadRotation());
    }

    DirectX::XMVECTOR GetForwardVector() const {
        return DirectX::XMVector3Rotate(
            DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
            LoadRotation());
    }

    static DirectX::XMFLOAT3 StoreVector3(DirectX::FXMVECTOR value) {
        DirectX::XMFLOAT3 result = {};
        DirectX::XMStoreFloat3(&result, value);
        return result;
    }

    void StoreRotationFromMatrix(DirectX::FXMMATRIX rotationMatrix) {
        DirectX::XMVECTOR q = DirectX::XMQuaternionRotationMatrix(rotationMatrix);
        q = DirectX::XMQuaternionNormalize(q);
        DirectX::XMStoreFloat4(&rotation, q);
    }



private:
    DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 0.0f, -5.0f);
    // 保存的是摄像机的旋转四元数，起始点默认是x:(1,0,0),y:(0,1,0),z:(0,0,1)的坐标系，旋转后得到新的坐标系。
    DirectX::XMFLOAT4 rotation = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

    float fovYRadians = DirectX::XM_PIDIV4;
    float aspectRatio = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;
};
