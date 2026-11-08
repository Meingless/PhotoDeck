use image::{GrayImage, Luma, Rgba, RgbaImage};
use photodeck::{
    document::{Document, validate_state},
    render,
    types::*,
};
use std::sync::Arc;
fn image(w: u32, h: u32, c: [u8; 4]) -> RgbaImage {
    RgbaImage::from_pixel(w, h, Rgba(c))
}
#[test]
fn failed_preview_and_cancel_keep_saved_identity() {
    let mut d = Document::new(Size::new(8, 8), false).unwrap();
    d.begin_edit().unwrap();
    d.state.layers[0].opacity = f64::NAN;
    assert!(d.commit("bad").is_err());
    assert_eq!(d.active_layer().opacity, 1.);
    assert!(!d.dirty);
    d.begin_edit().unwrap();
    d.state.layers[0].name = "preview".into();
    d.cancel_edit();
    assert_eq!(d.active_layer().name, "Arka plan");
    assert!(!d.can_undo());
}
#[test]
fn undo_to_saved_and_branch_are_consistent() {
    let mut d = Document::new(Size::new(8, 8), false).unwrap();
    d.add_layer("one").unwrap();
    d.mark_saved();
    d.add_layer("two").unwrap();
    assert!(d.dirty);
    d.undo();
    assert!(!d.dirty);
    d.undo();
    assert!(d.dirty);
    d.redo();
    assert!(!d.dirty);
    d.undo();
    d.add_layer("branch").unwrap();
    assert!(!d.can_redo());
    assert!(d.dirty);
}
#[test]
fn bounded_history_has_stable_revision_ids() {
    let mut d = Document::new(Size::new(2, 2), false).unwrap();
    let mut previous = 0;
    for i in 0..45 {
        d.transact("name", true, |s| {
            s.layers[0].name = format!("{i}");
            Ok(())
        })
        .unwrap();
        assert!(d.revision() > previous);
        previous = d.revision();
    }
    assert_eq!(d.history().len(), 30);
    assert!(!d.jump_history(1));
    let oldest = d.history()[0].0;
    assert!(d.jump_history(oldest));
    assert!(!d.can_undo());
    assert!(d.can_redo());
}
#[test]
fn canvas_resize_retains_offcanvas_source_and_mask() {
    let mut d = Document::new(Size::new(4, 4), true).unwrap();
    d.add_mask().unwrap();
    let pixels = d.active_layer().pixels.clone();
    let mask = d.active_layer().mask.clone().unwrap();
    d.canvas_size(Size::new(2, 2), Point::new(2, 2)).unwrap();
    assert_eq!(d.active_layer().offset, Point::new(-2, -2));
    assert!(Arc::ptr_eq(&pixels, &d.active_layer().pixels));
    assert!(Arc::ptr_eq(&mask, d.active_layer().mask.as_ref().unwrap()));
    d.undo();
    assert_eq!(d.state.size, Size::new(4, 4));
}
#[test]
fn hidden_smart_source_survives_merge_visible() {
    let mut d = Document::new(Size::new(4, 4), false).unwrap();
    d.convert_to_smart_object().unwrap();
    d.transact("hide", true, |s| {
        s.layers[0].visible = false;
        Ok(())
    })
    .unwrap();
    let id = d.active_layer().smart_object_id;
    d.insert_layer(Layer::new("one", image(4, 4, [255, 0, 0, 255])))
        .unwrap();
    d.insert_layer(Layer::new("two", image(4, 4, [0, 0, 255, 127])))
        .unwrap();
    let before = render::composite(&d.state);
    d.merge_visible().unwrap();
    assert_eq!(d.state.layers.len(), 2);
    assert_eq!(d.state.layers[0].smart_object_id, id);
    assert!(!d.state.layers[0].visible);
    assert_eq!(render::composite(&d.state), before);
}
#[test]
fn merge_visible_keeps_offcanvas_pixels_and_hidden_order() {
    let mut d = Document::new(Size::new(4, 4), false).unwrap();
    d.transact("base", true, |s| {
        s.layers[0].pixels = Arc::new(image(4, 4, [255, 0, 0, 255]));
        s.layers[0].offset = Point::new(-2, -1);
        Ok(())
    })
    .unwrap();
    let mut hidden = Layer::new("hidden", image(4, 4, [0, 255, 0, 255]));
    hidden.visible = false;
    d.insert_layer(hidden).unwrap();
    let mut top = Layer::new("top", image(4, 4, [0, 0, 255, 255]));
    top.offset = Point::new(3, 2);
    d.insert_layer(top).unwrap();
    let before = render::composite(&d.state);
    d.merge_visible().unwrap();
    assert_eq!(d.state.layers[0].name, "hidden");
    assert_eq!(d.active_layer().offset, Point::new(-2, -1));
    assert_eq!(d.active_layer().pixels.dimensions(), (9, 7));
    assert_eq!(render::composite(&d.state), before);
}
#[test]
fn clipping_merge_keeps_later_children_and_coverage() {
    let mut d = Document::new(Size::new(3, 3), false).unwrap();
    d.transact("base", true, |s| {
        s.layers[0].pixels = Arc::new(image(3, 3, [20, 30, 40, 100]));
        s.layers[0].opacity = 0.7;
        Ok(())
    })
    .unwrap();
    let mut top = Layer::new("clip", image(3, 3, [200, 80, 40, 220]));
    top.clipped = true;
    d.insert_layer(top).unwrap();
    let mut child = Layer::new("child", image(3, 3, [10, 150, 200, 90]));
    child.clipped = true;
    d.insert_layer(child).unwrap();
    d.state.active = 1;
    let before = render::composite(&d.state);
    d.merge_down().unwrap();
    assert_eq!(d.state.layers.len(), 2);
    assert!(d.state.layers[1].clipped);
    assert_eq!(d.active_layer().pixels.get_pixel(0, 0)[3], 100);
    assert_eq!(render::composite(&d.state), before);
}
#[test]
fn selection_copy_crops_and_cut_is_undoable() {
    let mut d = Document::new(Size::new(6, 6), true).unwrap();
    d.set_selection(Selection::Rectangle(Rect::new(2, 1, 3, 2)))
        .unwrap();
    d.layer_via_selection(true).unwrap();
    assert_eq!(d.active_layer().pixels.dimensions(), (3, 2));
    assert_eq!(d.active_layer().offset, Point::new(2, 1));
    assert_eq!(d.state.layers[0].pixels.get_pixel(2, 1)[3], 0);
    assert_eq!(d.state.layers[0].pixels.get_pixel(1, 1)[3], 255);
    d.undo();
    assert_eq!(d.state.layers.len(), 1);
    assert_eq!(d.active_layer().pixels.get_pixel(2, 1)[3], 255);
}
#[test]
fn detached_clipped_copy_bakes_coverage_without_mutating_document() {
    let mut d = Document::new(Size::new(4, 4), false).unwrap();
    d.transact("base", true, |s| {
        s.layers[0].pixels = Arc::new(image(4, 4, [40, 40, 40, 128]));
        s.layers[0].opacity = 0.5;
        Ok(())
    })
    .unwrap();
    let mut top = Layer::new("clip", image(4, 4, [200, 40, 40, 255]));
    top.clipped = true;
    d.insert_layer(top).unwrap();
    d.set_selection(Selection::Rectangle(Rect::new(0, 0, 2, 2)))
        .unwrap();
    let revision = d.revision();
    let before = d.state.clone();
    let pending = d.selection_copy_state().unwrap();
    assert_eq!(before, d.state);
    assert_eq!(revision, d.revision());
    assert!(!pending.layers[pending.active].clipped);
    assert_eq!(pending.layers[pending.active].pixels.get_pixel(0, 0)[3], 64);
}
#[test]
fn smart_replacement_mask_and_effect_frame_are_preserved() {
    let mut d = Document::new(Size::new(4, 4), true).unwrap();
    d.add_mask().unwrap();
    d.convert_to_smart_object().unwrap();
    d.transact("effects", true, |s| {
        s.layers[0].brightness = 20;
        Ok(())
    })
    .unwrap();
    let mask = d.active_layer().mask.clone().unwrap();
    d.replace_smart_object_contents(image(20, 10, [30, 40, 50, 255]))
        .unwrap();
    assert_eq!(d.active_layer().source_size(), Size::new(4, 4));
    assert!(Arc::ptr_eq(&mask, d.active_layer().mask.as_ref().unwrap()));
    assert_eq!(d.active_layer().brightness, 20);
    assert_eq!(render::render_layer(d.active_layer()).dimensions(), (4, 4));
}
#[test]
fn transformed_text_rasterize_is_explicit_and_reversible() {
    let mut d = Document::new(Size::new(20, 20), false).unwrap();
    d.add_text(
        "A",
        "Sans Serif,12,-1,5,400",
        [200, 100, 20, 255],
        Point::new(2, 3),
    )
    .unwrap();
    let original = d.active_layer().pixels.clone();
    d.set_layer_transform(Transform::scale(0.5, 0.5)).unwrap();
    let before = render::composite(&d.state);
    d.rasterize_layer().unwrap();
    assert!(d.active_layer().text.is_empty());
    assert!(d.active_layer().geometry.is_identity());
    assert_eq!(render::composite(&d.state), before);
    d.undo();
    assert_eq!(d.active_layer().text, "A");
    assert!(Arc::ptr_eq(&original, &d.active_layer().pixels));
}
#[test]
fn invalid_matrices_and_duplicate_smart_ids_are_rejected() {
    let mut d = Document::new(Size::new(4, 4), false).unwrap();
    d.convert_to_smart_object().unwrap();
    let mut state = d.state.clone();
    state.layers.push(state.layers[0].clone());
    assert!(validate_state(&state).is_err());
    let before = d.state.clone();
    assert!(d.set_layer_transform(Transform([0.; 9])).is_err());
    assert_eq!(d.state, before);
    assert!(
        d.set_layer_transform(Transform::translate(MAX_COORDINATE as f64 - 1., 0.))
            .is_err()
    );
}
#[test]
fn soft_mask_density_and_disable_do_not_modify_mask() {
    let mut d = Document::new(Size::new(2, 2), true).unwrap();
    d.transact("mask", true, |s| {
        s.layers[0].mask = Some(Arc::new(GrayImage::from_pixel(2, 2, Luma([0]))));
        s.layers[0].mask_settings.density = 50;
        Ok(())
    })
    .unwrap();
    assert_eq!(render::composite(&d.state).get_pixel(0, 0)[3], 127);
    let mask = d.active_layer().mask.clone().unwrap();
    d.transact("disabled", true, |s| {
        s.layers[0].mask_settings.enabled = false;
        Ok(())
    })
    .unwrap();
    assert_eq!(render::composite(&d.state).get_pixel(0, 0)[3], 255);
    assert!(Arc::ptr_eq(&mask, d.active_layer().mask.as_ref().unwrap()));
}
#[test]
fn oversized_odd_alignment_rounds_to_top_left_and_hidden_move_is_inert() {
    use photodeck::document::{Alignment, AlignmentTarget};
    let mut d = Document::new(Size::new(4, 4), false).unwrap();
    d.insert_layer(Layer::new("oversized", image(7, 7, [255; 4])))
        .unwrap();
    d.align_layer(Alignment::Center, AlignmentTarget::Canvas)
        .unwrap();
    d.align_layer(Alignment::Middle, AlignmentTarget::Canvas)
        .unwrap();
    assert_eq!(d.active_layer().offset, Point::new(-2, -2));
    d.transact("hide", true, |s| {
        s.layers[s.active].visible = false;
        Ok(())
    })
    .unwrap();
    let revision = d.revision();
    assert!(d.nudge_layer(Point::new(1, 0)).is_err());
    assert_eq!(d.revision(), revision);
}
#[test]
fn qt_smart_transform_oracle_preserves_boundary_coverage() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures");
    let (doc, _) = photodeck::project::open(&root.join("qt-v7.pdeck")).unwrap();
    let actual = render::composite(&doc.state);
    let reference = image::open(root.join("qt-v7.png")).unwrap().into_rgba8();
    assert_eq!(actual.dimensions(), reference.dimensions());
    for (actual, reference) in actual.pixels().zip(reference.pixels()) {
        assert_eq!(
            actual[3], reference[3],
            "transformed edge must not fade from transparent sampling taps"
        );
        // The legacy painter rounds through premultiplied 8-bit intermediates,
        // whereas the Rust color pipeline retains straight channels between passes.
        for channel in 0..3 {
            assert!((actual[channel] as i16 - reference[channel] as i16).abs() <= 6);
        }
    }
}
