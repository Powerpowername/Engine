
static const float PI = 3.14159265359;

float distributionGGX (float3 N, float3 H, float roughness){
    float a2    = roughness * roughness * roughness * roughness;
    float NdotH = max (dot (N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float geometrySchlickGGX (float NdotV, float roughness){
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith (float3 N, float3 V, float3 L, float roughness){
    return geometrySchlickGGX (max (dot (N, L), 0.0), roughness) * 
           geometrySchlickGGX (max (dot (N, V), 0.0), roughness);
}

float3 fresnelSchlick (float cosTheta, float3 F0){
    return F0 + (1.0 - F0) * pow (1.0 - cosTheta, 5.0);
}

// https://www.shadertoy.com/view/4d2XWV by Inigo Quilez
// 光线与球体相交的函数，返回值是距离，负值在光线后面，丢弃
float sphereIntersect(float3 ro, float3 rd, float4 sph) {
	float3 oc = ro - sph.xyz;
	float b = dot( oc, rd );
	float c = dot( oc, oc ) - sph.w*sph.w;
	float h = b*b - c;
	if( h<0.0 ) return -1.0;
	return -b - sqrt( h );
}
