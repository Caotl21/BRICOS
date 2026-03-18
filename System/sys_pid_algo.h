// 单个 PID 控制器结构体
typedef struct {
    float kp, ki, kd;
    float error_int;     // 积分累加器
    float error_last;    // 上次误差 (用于微分)
    float integral_max;  // 积分限幅 (抗积分饱和防炸机)
    float output_max;    // 输出限幅
} PID_Controller_t;

// 串级 PID 控制器 (包含外环和内环)
typedef struct {
    PID_Controller_t outer; // 角度环
    PID_Controller_t inner; // 角速度环
} Cascade_PID_t;

// 机器人的 6 自由度控制指令池
typedef struct {
    float force_x;   // 前后平移推力 (Surge)
    float force_y;   // 左右平移推力 (Sway)
    float force_z;   // 上下垂直推力 (Heave)
    float torque_x;  // 横滚扭矩 (Roll)
    float torque_y;  // 俯仰扭矩 (Pitch)
    float torque_z;  // 偏航扭矩 (Yaw)
} Bot_Wrench_t; // 物理学中称为 Wrench (旋量)