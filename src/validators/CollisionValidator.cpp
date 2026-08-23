#include "../../src/validators/CollisionValidator.hpp"

CollisionValidator::CollisionValidator()
{
}

CollisionValidator::CollisionValidator(const CollisionValidator &other)
{
    *this = other;
}

CollisionValidator::~CollisionValidator()
{
}

CollisionValidator &CollisionValidator::operator=(const CollisionValidator &other)
{
    (void)other;
    return (*this);
}

int CollisionValidator::test_movement(World &world, Camera &camera, Metrics &m)
{
    CameraInput input;

    camera.initialize();
    PlayerController::spawn_player_on_ground(&camera, world);
    m.start_x = camera.x;
    m.start_z = camera.z;
    input = InputReader::empty_camera_input();
    input.move_forward = true;
    PlayerController::update_player_horizontal_motion(&camera, input, world, 1.0);
    m.movement_delta_z = camera.z - m.start_z;
    if (std::fabs(camera.z - m.start_z) < 0.25)
    {
        std::fprintf(stderr, "collision: failed movement sanity check\n");
        return (1);
    }
    return (0);
}

int CollisionValidator::test_wall(World &world, Camera &camera, Metrics &m)
{
    CameraInput input;
    double      before_wall_x;

    before_wall_x = camera.x;
    (void)world.place_block_at(static_cast<int32_t>(std::floor(camera.x + 1.0)),
                               static_cast<int32_t>(std::floor(camera.y - 1.0)),
                               static_cast<int32_t>(std::floor(camera.z)),
                               TERRAIN_GENERATOR_STONE_BLOCK);
    input = InputReader::empty_camera_input();
    input.move_right = true;
    PlayerController::update_player_horizontal_motion(&camera, input, world, 0.25);
    m.wall_delta_x = camera.x - before_wall_x;
    if (camera.x > before_wall_x + 0.75)
    {
        std::fprintf(stderr, "collision: failed wall rejection check\n");
        return (1);
    }
    return (0);
}

int CollisionValidator::setup_step_blocks(World   &world,
                                          Camera  &camera,
                                          int32_t &step_x,
                                          int32_t &step_y,
                                          int32_t &step_z)
{
    int32_t frame;
    int32_t error_code;

    camera.initialize();
    PlayerController::spawn_player_on_ground(&camera, world);
    step_x = static_cast<int32_t>(std::floor(camera.x));
    step_z = static_cast<int32_t>(std::floor(camera.z + 1.0));
    step_y = static_cast<int32_t>(
        std::floor(camera.y - PlayerController::PLAYER_EYE_HEIGHT));
    frame = 0;
    while (frame < 4)
    {
        error_code = world.place_block_at(step_x, step_y, step_z + frame,
                                          TERRAIN_GENERATOR_STONE_BLOCK);
        if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_ALREADY_EXISTS)
            return (ApplicationError::fail("collision step setup", error_code));
        frame = frame + 1;
    }
    return (0);
}

bool CollisionValidator::run_climb_loop(Camera &camera,
                                        World  &world,
                                        double  start_y,
                                        double &climb_delta_y)
{
    CameraInput input;
    int32_t     frame;
    bool        climbed;

    input = InputReader::empty_camera_input();
    input.move_forward = true;
    input.jump_pressed = true;
    frame = 0;
    climbed = false;
    climb_delta_y = 0.0;
    while (frame < 120)
    {
        PlayerController::update_player_vertical_motion(&camera, input, world,
                                                        1.0 / 60.0);
        PlayerController::update_player_horizontal_motion(&camera, input, world,
                                                          1.0 / 60.0);
        if (camera.y - start_y > climb_delta_y)
            climb_delta_y = camera.y - start_y;
        if (camera.y >= start_y + 0.75 && camera.on_ground == true)
            climbed = true;
        input.jump_pressed = false;
        frame = frame + 1;
    }
    return (climbed);
}

int CollisionValidator::test_step_climb(World &world, Camera &camera, Metrics &m)
{
    int32_t step_x;
    int32_t step_y;
    int32_t step_z;
    double  climb_start_y;

    if (setup_step_blocks(world, camera, step_x, step_y, step_z) != 0)
        return (1);
    climb_start_y = camera.y;
    if (run_climb_loop(camera, world, climb_start_y, m.climb_delta_y) == false)
    {
        std::fprintf(stderr,
                     "collision: failed one-block climb check"
                     " y=%.2f start=%.2f ground=%d\n",
                     camera.y, climb_start_y, camera.on_ground == true ? 1 : 0);
        return (1);
    }
    return (0);
}

void CollisionValidator::setup_tunnel(World   &world,
                                      Camera  &camera,
                                      int32_t &tx,
                                      int32_t &ty,
                                      int32_t &tz)
{
    int32_t frame;

    camera.initialize();
    PlayerController::spawn_player_on_ground(&camera, world);
    tx = static_cast<int32_t>(std::floor(camera.x));
    ty = static_cast<int32_t>(
        std::floor(camera.y - PlayerController::PLAYER_EYE_HEIGHT));
    tz = static_cast<int32_t>(std::floor(camera.z));
    frame = 0;
    while (frame < 6)
    {
        (void)world.delete_block_at(tx, ty, tz + frame);
        (void)world.delete_block_at(tx, ty + 1, tz + frame);
        (void)world.place_block_at(tx - 1, ty, tz + frame,
                                   TERRAIN_GENERATOR_STONE_BLOCK);
        (void)world.place_block_at(tx - 1, ty + 1, tz + frame,
                                   TERRAIN_GENERATOR_STONE_BLOCK);
        (void)world.place_block_at(tx + 1, ty, tz + frame,
                                   TERRAIN_GENERATOR_STONE_BLOCK);
        (void)world.place_block_at(tx + 1, ty + 1, tz + frame,
                                   TERRAIN_GENERATOR_STONE_BLOCK);
        frame = frame + 1;
    }
}

int CollisionValidator::test_tunnel(World &world, Camera &camera, Metrics &m)
{
    CameraInput input;
    int32_t     tx;
    int32_t     ty;
    int32_t     tz;
    double      tunnel_start_z;
    int32_t     frame;

    setup_tunnel(world, camera, tx, ty, tz);
    tunnel_start_z = camera.z;
    input = InputReader::empty_camera_input();
    input.move_forward = true;
    frame = 0;
    while (frame < 90)
    {
        PlayerController::update_player_vertical_motion(&camera, input, world,
                                                        1.0 / 60.0);
        PlayerController::update_player_horizontal_motion(&camera, input, world,
                                                          1.0 / 60.0);
        frame = frame + 1;
    }
    m.tunnel_delta_z = camera.z - tunnel_start_z;
    if (m.tunnel_delta_z < 1.0)
    {
        std::fprintf(stderr,
                     "collision: failed one-block-wide tunnel check"
                     " start=%.2f end=%.2f\n",
                     tunnel_start_z, camera.z);
        return (1);
    }
    return (0);
}

void CollisionValidator::setup_drop(World  &world,
                                    Camera &camera,
                                    double &drop_start_y)
{
    int32_t drop_x;
    int32_t drop_y;
    int32_t drop_z;
    int32_t frame;

    camera.initialize();
    PlayerController::spawn_player_on_ground(&camera, world);
    drop_start_y = camera.y;
    drop_x = static_cast<int32_t>(std::floor(camera.x));
    drop_y = static_cast<int32_t>(
                 std::floor(camera.y - PlayerController::PLAYER_EYE_HEIGHT))
             - 1;
    drop_z = static_cast<int32_t>(std::floor(camera.z));
    frame = 0;
    while (frame < 5)
    {
        (void)world.delete_block_at(drop_x, drop_y - frame, drop_z);
        frame = frame + 1;
    }
    camera.on_ground = false;
    camera.vertical_velocity = 0.0;
}

int CollisionValidator::test_drop(World &world, Camera &camera, Metrics &m)
{
    CameraInput input;
    double      drop_start_y;
    int32_t     frame;

    setup_drop(world, camera, drop_start_y);
    input = InputReader::empty_camera_input();
    frame = 0;
    while (frame < 90)
    {
        PlayerController::update_player_vertical_motion(&camera, input, world,
                                                        1.0 / 60.0);
        PlayerController::update_player_horizontal_motion(&camera, input, world,
                                                          1.0 / 60.0);
        frame = frame + 1;
    }
    m.drop_delta_y = drop_start_y - camera.y;
    if (camera.y > drop_start_y - 1.0)
    {
        std::fprintf(stderr,
                     "collision: failed one-block drop shaft check"
                     " start_y=%.2f end_y=%.2f\n",
                     drop_start_y, camera.y);
        return (1);
    }
    return (0);
}

int CollisionValidator::validate() const
{
    World   world;
    Camera  camera;
    Metrics m;
    int32_t error_code;

    error_code = world.initialize("integration-seed");
    if (error_code != FT_ERR_SUCCESS)
        return (ApplicationError::fail("collision world initialization", error_code));
    if (test_movement(world, camera, m) != 0 || test_wall(world, camera, m) != 0
        || test_step_climb(world, camera, m) != 0
        || test_tunnel(world, camera, m) != 0 || test_drop(world, camera, m) != 0)
    {
        world.destroy();
        return (1);
    }
    std::printf("collision: ok start=(%.2f,%.2f) moved_z=%.2f wall_x=%.2f"
                " climb_y=%.2f tunnel_z=%.2f drop_y=%.2f\n",
                m.start_x, m.start_z, m.movement_delta_z, m.wall_delta_x,
                m.climb_delta_y, m.tunnel_delta_z, m.drop_delta_y);
    world.destroy();
    return (0);
}
