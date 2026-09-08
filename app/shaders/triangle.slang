struct PushData
{
	float4 tint;
};

[vk::push_constant]
PushData g_push;

struct VSInput 
{
    float3 position : POSITION;
    float4 color : COLOR0;
};

struct VSOutput
{
	float4 position : SV_Position;
	float4 color : COLOR0;
};

[shader("vertex")]
VSOutput vsMain(VSInput input)
{
    VSOutput o;
    o.position = float4(input.position, 1.0);
    o.color = input.color * g_push.tint;
	return o;
}

[shader("fragment")]
float4 fsMain(float4 color : COLOR0) : SV_Target
{
	return color;
}
