 #include "gfx/game_widget.h"
#include "game/obj_bearded_man.h"

#include <cmath>

namespace bm {

void GameWidget::initialize() {
    terrain_shader_ = Loader::load_shader("colored_blocks");
    rgb_vox_shader_ = Loader::load_shader("rgb_blocks");

    //
    // World setup and update terrain
    //
    //vol_ = std::make_unique<WorldPager>();
    //vol_slice_ = std::make_unique<SlabVolume>(vol_.get(), 64 * 1048576, 64);
    pv::Region world_reg(Vec3i(0, 0, 0),
                         Vec3i(WORLDSZ_X, WORLDSZ_Y, WORLDSZ_Z));
    volume_ = std::make_unique<RawVolume>(world_reg);
    populate::populate_voxels(world_reg, *volume_);

    //
    // Load models
    //
    load_model(ModelId::Cursor,
                         "assets/model/cursor.qb", rgb_vox_shader_);
    load_model(ModelId::CursorRed,
                             "assets/model/cursor_red.qb", rgb_vox_shader_);
    cursor_pos_ = Vec3i(VIEWSZ_X / 2, 4, VIEWSZ_Z / 2);

    world_ = std::make_unique<World>(*volume_);

    // Spawn 7 bearded men
    load_model(ModelId::BeardedMan, "assets/model/dorf.qb", rgb_vox_shader_);
    const int MANY_BEARDED_MEN = 1;
    for (auto bm = 0; bm < MANY_BEARDED_MEN; ++bm) {
        world_->add(new BeardedMan(cursor_pos_ + Vec3i(bm, 0, bm)));
    }

    load_model(ModelId::Wood, "assets/model/wood.qb", rgb_vox_shader_);
    auto xyz = load_model(ModelId::Xyz, "assets/model/xyz.qb", rgb_vox_shader_);
    xyz->mesh_->scale_ *= 2.0f;
    xyz->mesh_->translation_ = QVector3D(-1.0f, -1.0f, -1.0f); // pivot

    camera_follow_cursor();
    update_terrain_model();

    // Keyboard input mode
    change_keyboard_fsm(KeyFSM::Default);
}

// Extract the surface
void GameWidget::update_terrain_model() {
    auto org_y = cursor_pos_.getY();
    pv::Region reg2(Vec3i(0, org_y, 0),
                    Vec3i(VIEWSZ_X, org_y + VIEWSZ_Y - 1, VIEWSZ_Z));

    auto raw_mesh = pv::extractCubicMesh(
                volume_.get(), reg2, TerrainIsQuadNeeded(), true);
    //auto mesh = pv::extractMarchingCubesMesh(&vol_slice, reg2);
    std::cout << "terrain mesh #vertices: " << raw_mesh.getNoOfVertices()
              << std::endl;

    auto decoded_mesh = pv::decodeMesh(raw_mesh);

    // Pass the surface to the OpenGL window
    //
    terrain_.release();
    terrain_ = std::make_unique<Model>(
                Loader::create_opengl_mesh_from_raw(this, decoded_mesh),
                terrain_shader_);
    terrain_->mesh_->scale_.setY(-WALL_HEIGHT);
    // move terrain slab together with cursor
    terrain_->mesh_->translation_.setY(-cursor_pos_.getY());

    this->update();
} // upd terrain

// Returns pointer for temporary use and modification, do not store permanently
GameWidget::GameWidget(QWidget *parent)
    : GLVersion_Widget(parent), lua_(true) {
    //lua_.Load("scripts/game.lua");
}

Model *GameWidget::load_model(ModelId register_as,
                              const char *file,
                             ShaderPtr shad) {
    auto qb_model = std::make_unique<QBFile>(file);
    auto raw_mesh = qb_model->get_mesh_for_volume(0);
    qb_model->free_voxels_for_volume(0);

    auto opengl_mesh = Loader::create_opengl_mesh_from_raw(
                this,
                raw_mesh,
                Vec3f(0.f, 0.f, 0.f),
                qb_model->get_downscale(0)
                );

    models_[register_as] = Model(opengl_mesh, shad);
    return &(models_[register_as]);
}

const Model *GameWidget::find_model(ModelId id) const
{
    auto iter = models_.find(id);
    if (iter == models_.end()) { return nullptr; }
    return &iter->second;
}

void GameWidget::render_frame() {
    render(*terrain_, Vec3f(0.f, 0.f, 0.f), 0.f);
    //dorf_.render(this, pos_for_cell(dorf_pos_), 0.f);

    world_->each_obj([this](auto /*id*/, auto co) {
        auto ent = co->as_entity();
        if (ent) {
            auto model_id = ent->get_model_id();
            if (model_id != ModelId::NIL) {
                auto m = this->find_model(model_id);
                Q_ASSERT(m);
                this->render(*m, pos_for_cell(ent->get_pos()), 0.0f);
            }
        }
    });

    //wood_.render(this, pos_for_cell(dorf_pos_+Vec3i(0,0,2)), 0.f);

    auto cursor = models_.find(ModelId::Cursor);
    render(cursor->second, pos_for_cell(cursor_pos_), 0.f);

    //render_overlay_xyz();
}

void GameWidget::render(const Model& m, const Vec3f &pos, float rot_y)
{
    if (not m.mesh_->is_valid()) {
        return;
    }

    m.shad_->bind();

    // These two matrices are constant for all meshes.
    m.shad_->setUniformValue("viewMatrix", this->get_view_matrix());
    m.shad_->setUniformValue("projectionMatrix", this->get_projection_matrix());

    // Set up the model matrrix based on provided translation and scale.
    //
    QMatrix4x4 model_mx;
    model_mx.translate(m.mesh_->translation_
                       + QVector3D(pos.getX(), pos.getY(), pos.getZ()));
    model_mx.scale(m.mesh_->scale_);
    model_mx.rotate(rot_y + m.mesh_->rotation_y_, 0.f, 1.f, 0.f);

    m.shad_->setUniformValue("modelMatrix", model_mx);

    // Bind the vertex array for the current mesh
    glBindVertexArray(m.mesh_->vert_array_);
    // Draw the mesh
    glDrawElements(GL_TRIANGLES,
                   m.mesh_->indx_count_,
                   m.mesh_->indx_type_, 0);
    // Unbind the vertex array.
    glBindVertexArray(0);

    // We're done with the shader for this frame.
    m.shad_->release();
}

void GameWidget::render_overlay_xyz() {
    glDisable(GL_DEPTH_TEST);
    glPushAttrib(GL_VIEWPORT_BIT);
    glViewport(0, 0,
               this->geometry().width() * 0.25f,
               this->geometry().height() * 0.25f);

    QVector3D cam_f = this->get_cam_forward() * 5.0f;

    view_matrix_.setToIdentity();
    view_matrix_.lookAt(
        -cam_f,  // Camera is at
        QVector3D(0.f, 0.f, 0.f),  // Look at
        QVector3D(0.f, 1.f, 0.f)   // Camera up
        );

    auto xyz = find_model(ModelId::Xyz);
    Q_ASSERT(xyz);
    render(*xyz, Vec3f(0.f, 0.f, 0.f), -cam_yaw_);

    glPopAttrib();
    glEnable(GL_DEPTH_TEST);
}

void GameWidget::camera_follow_cursor()
{
    QVector3D cam_pos((cursor_pos_.getX() + 5) * CELL_SIZE,
                      (15.0f - cursor_pos_.getY()) * WALL_HEIGHT, // up
                      (cursor_pos_.getZ() + 5) * CELL_SIZE);

    set_camera_transform(cam_pos,
                       -7.0 * M_PI / 18.0, //pitch (minus - look down)
                       M_PI); // yaw
    on_cursor_changed();
}

void GameWidget::on_cursor_changed()
{
    emit SIG_cursor_changed(QPoint(cursor_pos_.getX(),
                                   cursor_pos_.getZ()),
                            cursor_pos_.getY());
}

void GameWidget::change_keyboard_fsm(bm::KeyFSM fsm_state)
{
    switch (fsm_state) {
    case KeyFSM::Default:
        keyboard_handler_ = &GameWidget::fsm_keypress_exploremap;
        break;
    case KeyFSM::Orders:
        keyboard_handler_ = &GameWidget::fsm_keypress_orders;
        break;
    case KeyFSM::Digging:
        keyboard_handler_ = &GameWidget::fsm_keypress_digging;
        break;
    }
    emit SIG_keyboard_fsm_changed(fsm_state);
}

// Normal keyboard mode where you can navigate camera and map, possibly switch
// to different modes.
void GameWidget::fsm_keypress_exploremap(QKeyEvent *event) {
    switch ( event->key() ) {
    case Qt::Key_Right:
        if (cursor_pos_.getX() < WORLDSZ_X - 1) {
            cursor_pos_ += Vec3i(1, 0, 0);
            camera_follow_cursor();
        }
        break;
    case Qt::Key_Left:
        if (cursor_pos_.getX() > 0) {
            cursor_pos_ += Vec3i(-1, 0, 0);
            camera_follow_cursor();
        }
        break;
    case Qt::Key_Down:
        if (cursor_pos_.getZ() < WORLDSZ_Z - 1) {
            cursor_pos_ += Vec3i(0, 0, 1);
            camera_follow_cursor();
        }
        break;
    case Qt::Key_Up:
        if (cursor_pos_.getZ() > 0) {
            cursor_pos_ += Vec3i(0, 0, -1);
            camera_follow_cursor();
        }
        break;
    case Qt::Key_Minus:
        if (cursor_pos_.getY() > 0) {
            cursor_pos_ += Vec3i(0, -1, 0);
            // this is to emit ui update, camera doesn't really move
            camera_follow_cursor();
//            on_cursor_changed();
            update_terrain_model();
        }
        break;
    case Qt::Key_Plus:
        if (cursor_pos_.getY() < WORLDSZ_Y - 1) {
            cursor_pos_ += Vec3i(0, 1, 0);
            // this is to emit ui update, camera doesn't really move
            camera_follow_cursor();
//            on_cursor_changed();
            update_terrain_model();
        }
        break;
//    case Qt::Key_O: {
//        change_keyboard_fsm(KeyFSM::Orders);
//        break;
//    }
    case Qt::Key_D: {
        change_keyboard_fsm(KeyFSM::Digging);
        break;
    }
    case Qt::Key_Period: {
        world_->think();
        if (world_->force_update_terrain_mesh_) {
            update_terrain_model();
            world_->force_update_terrain_mesh_ = false;
        }
        this->update();
    } break;

    case Qt::Key_W:
    case Qt::Key_S:
    case Qt::Key_A:
//    case Qt::Key_D:
//    case Qt::Key_Escape:
        return GLVersion_Widget::keyPressEvent(event);
    default:
        event->ignore();
        break;
    }
}

void GameWidget::fsm_keypress_orders(QKeyEvent *event) {
    switch ( event->key() ) {
    case Qt::Key_Escape:
        change_keyboard_fsm(KeyFSM::Default);
        break;
    case Qt::Key_W:
    case Qt::Key_S:
    case Qt::Key_A:
    case Qt::Key_D:
        return GLVersion_Widget::keyPressEvent(event);
    default:
        event->ignore();
        break;
    }
}

void GameWidget::fsm_keypress_digging(QKeyEvent *event)
{
    switch ( event->key() ) {
    case Qt::Key_Escape:
        change_keyboard_fsm(KeyFSM::Default);
        break;
    case Qt::Key_D: {
        // {D}esignations -> {D} mine
        // Records player's wish to have current cell mined out
        world_->add_mining_goal(cursor_pos_);
        // Order accepted, return to default
        change_keyboard_fsm(KeyFSM::Default);
    } break;
//    case Qt::Key_W:case Qt::Key_S:case Qt::Key_A:case Qt::Key_D:
//        return GLVersion_Widget::keyPressEvent(event);
    default:
        event->ignore();
        break;
    }
}

} // namespace bm
