/*************************************************************************/
/*  animation_track_editor.cpp                                           */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "animation_track_editor.h"

#include "animation_track_editor_plugins.h"
#include "core/input/input.h"
#include "editor/animation_bezier_editor.h"
#include "editor/editor_node.h"
#include "editor/editor_scale.h"
#include "editor/plugins/animation_player_editor_plugin.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/view_panner.h"
#include "scene/main/window.h"
#include "scene/scene_string_names.h"
#include "servers/audio/audio_stream.h"

class AnimationTrackKeyEdit : public Object {
	GDCLASS(AnimationTrackKeyEdit, Object);

public:
	bool setting = false;

	bool _hide_script_from_inspector() {
		return true;
	}

	bool _dont_undo_redo() {
		return true;
	}

	static void _bind_methods() {
		ClassDB::bind_method("_update_obj", &AnimationTrackKeyEdit::_update_obj);
		ClassDB::bind_method("_key_ofs_changed", &AnimationTrackKeyEdit::_key_ofs_changed);
		ClassDB::bind_method("_hide_script_from_inspector", &AnimationTrackKeyEdit::_hide_script_from_inspector);
		ClassDB::bind_method("get_root_path", &AnimationTrackKeyEdit::get_root_path);
		ClassDB::bind_method("_dont_undo_redo", &AnimationTrackKeyEdit::_dont_undo_redo);
	}

	void _fix_node_path(Variant &value) {
		NodePath np = value;

		if (np == NodePath()) {
			return;
		}

		Node *root = EditorNode::get_singleton()->get_tree()->get_root();

		Node *np_node = root->get_node(np);
		ERR_FAIL_COND(!np_node);

		Node *edited_node = root->get_node(base);
		ERR_FAIL_COND(!edited_node);

		value = edited_node->get_path_to(np_node);
	}

	void _update_obj(const Ref<Animation> &p_anim) {
		if (setting || animation != p_anim) {
			return;
		}

		notify_change();
	}

	void _key_ofs_changed(const Ref<Animation> &p_anim, float from, float to) {
		if (animation != p_anim || from != key_ofs) {
			return;
		}

		key_ofs = to;

		if (setting) {
			return;
		}

		notify_change();
	}

	bool _set(const StringName &p_name, const Variant &p_value) {
		int key = animation->track_find_key(track, key_ofs, true);
		ERR_FAIL_COND_V(key == -1, false);

		String name = p_name;
		if (name == "time" || name == "frame") {
			float new_time = p_value;

			if (name == "frame") {
				float fps = animation->get_step();
				if (fps > 0) {
					fps = 1.0 / fps;
				}
				new_time /= fps;
			}

			if (new_time == key_ofs) {
				return true;
			}

			int existing = animation->track_find_key(track, new_time, true);

			setting = true;
			undo_redo->create_action(TTR("Anim Change Keyframe Time"), UndoRedo::MERGE_ENDS);

			Variant val = animation->track_get_key_value(track, key);
			float trans = animation->track_get_key_transition(track, key);

			undo_redo->add_do_method(animation.ptr(), "track_remove_key", track, key);
			undo_redo->add_do_method(animation.ptr(), "track_insert_key", track, new_time, val, trans);
			undo_redo->add_do_method(this, "_key_ofs_changed", animation, key_ofs, new_time);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", track, new_time);
			undo_redo->add_undo_method(animation.ptr(), "track_insert_key", track, key_ofs, val, trans);
			undo_redo->add_undo_method(this, "_key_ofs_changed", animation, new_time, key_ofs);

			if (existing != -1) {
				Variant v = animation->track_get_key_value(track, existing);
				trans = animation->track_get_key_transition(track, existing);
				undo_redo->add_undo_method(animation.ptr(), "track_insert_key", track, new_time, v, trans);
			}
			undo_redo->commit_action();

			setting = false;
			return true;
		}

		if (name == "easing") {
			float val = p_value;
			float prev_val = animation->track_get_key_transition(track, key);
			setting = true;
			undo_redo->create_action(TTR("Anim Change Transition"), UndoRedo::MERGE_ENDS);
			undo_redo->add_do_method(animation.ptr(), "track_set_key_transition", track, key, val);
			undo_redo->add_undo_method(animation.ptr(), "track_set_key_transition", track, key, prev_val);
			undo_redo->add_do_method(this, "_update_obj", animation);
			undo_redo->add_undo_method(this, "_update_obj", animation);
			undo_redo->commit_action();

			setting = false;
			return true;
		}

		switch (animation->track_get_type(track)) {
			case Animation::TYPE_POSITION_3D:
			case Animation::TYPE_ROTATION_3D:
			case Animation::TYPE_SCALE_3D: {
				if (name == "position" || name == "rotation" || name == "scale") {
					Variant old = animation->track_get_key_value(track, key);
					setting = true;
					String chan;
					switch (animation->track_get_type(track)) {
						case Animation::TYPE_POSITION_3D:
							chan = "Position3D";
							break;
						case Animation::TYPE_ROTATION_3D:
							chan = "Rotation3D";
							break;
						case Animation::TYPE_SCALE_3D:
							chan = "Scale3D";
							break;
						default: {
						}
					}

					undo_redo->create_action(vformat(TTR("Anim Change %s"), chan));
					undo_redo->add_do_method(animation.ptr(), "track_set_key_value", track, key, p_value);
					undo_redo->add_undo_method(animation.ptr(), "track_set_key_value", track, key, old);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}

			} break;
			case Animation::TYPE_BLEND_SHAPE:
			case Animation::TYPE_VALUE: {
				if (name == "value") {
					Variant value = p_value;

					if (value.get_type() == Variant::NODE_PATH) {
						_fix_node_path(value);
					}

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					Variant prev = animation->track_get_key_value(track, key);
					undo_redo->add_do_method(animation.ptr(), "track_set_key_value", track, key, value);
					undo_redo->add_undo_method(animation.ptr(), "track_set_key_value", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}
			} break;
			case Animation::TYPE_METHOD: {
				Dictionary d_old = animation->track_get_key_value(track, key);
				Dictionary d_new = d_old.duplicate();

				bool change_notify_deserved = false;
				bool mergeable = false;

				if (name == "name") {
					d_new["method"] = p_value;
				} else if (name == "arg_count") {
					Vector<Variant> args = d_old["args"];
					args.resize(p_value);
					d_new["args"] = args;
					change_notify_deserved = true;
				} else if (name.begins_with("args/")) {
					Vector<Variant> args = d_old["args"];
					int idx = name.get_slice("/", 1).to_int();
					ERR_FAIL_INDEX_V(idx, args.size(), false);

					String what = name.get_slice("/", 2);
					if (what == "type") {
						Variant::Type t = Variant::Type(int(p_value));

						if (t != args[idx].get_type()) {
							Callable::CallError err;
							if (Variant::can_convert(args[idx].get_type(), t)) {
								Variant old = args[idx];
								Variant *ptrs[1] = { &old };
								Variant::construct(t, args.write[idx], (const Variant **)ptrs, 1, err);
							} else {
								Variant::construct(t, args.write[idx], nullptr, 0, err);
							}
							change_notify_deserved = true;
							d_new["args"] = args;
						}
					} else if (what == "value") {
						Variant value = p_value;
						if (value.get_type() == Variant::NODE_PATH) {
							_fix_node_path(value);
						}

						args.write[idx] = value;
						d_new["args"] = args;
						mergeable = true;
					}
				}

				if (mergeable) {
					undo_redo->create_action(TTR("Anim Change Call"), UndoRedo::MERGE_ENDS);
				} else {
					undo_redo->create_action(TTR("Anim Change Call"));
				}

				setting = true;
				undo_redo->add_do_method(animation.ptr(), "track_set_key_value", track, key, d_new);
				undo_redo->add_undo_method(animation.ptr(), "track_set_key_value", track, key, d_old);
				undo_redo->add_do_method(this, "_update_obj", animation);
				undo_redo->add_undo_method(this, "_update_obj", animation);
				undo_redo->commit_action();

				setting = false;
				if (change_notify_deserved) {
					notify_change();
				}
				return true;
			} break;
			case Animation::TYPE_BEZIER: {
				if (name == "value") {
					const Variant &value = p_value;

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					float prev = animation->bezier_track_get_key_value(track, key);
					undo_redo->add_do_method(animation.ptr(), "bezier_track_set_key_value", track, key, value);
					undo_redo->add_undo_method(animation.ptr(), "bezier_track_set_key_value", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}

				if (name == "in_handle") {
					const Variant &value = p_value;

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					Vector2 prev = animation->bezier_track_get_key_in_handle(track, key);
					undo_redo->add_do_method(animation.ptr(), "bezier_track_set_key_in_handle", track, key, value);
					undo_redo->add_undo_method(animation.ptr(), "bezier_track_set_key_in_handle", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}

				if (name == "out_handle") {
					const Variant &value = p_value;

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					Vector2 prev = animation->bezier_track_get_key_out_handle(track, key);
					undo_redo->add_do_method(animation.ptr(), "bezier_track_set_key_out_handle", track, key, value);
					undo_redo->add_undo_method(animation.ptr(), "bezier_track_set_key_out_handle", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}

				if (name == "handle_mode") {
					const Variant &value = p_value;

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					int prev = animation->bezier_track_get_key_handle_mode(track, key);
					undo_redo->add_do_method(animation.ptr(), "bezier_track_set_key_handle_mode", track, key, value);
					undo_redo->add_undo_method(animation.ptr(), "bezier_track_set_key_handle_mode", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}
			} break;
			case Animation::TYPE_AUDIO: {
				if (name == "stream") {
					Ref<AudioStream> stream = p_value;

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					Ref<Resource> prev = animation->audio_track_get_key_stream(track, key);
					undo_redo->add_do_method(animation.ptr(), "audio_track_set_key_stream", track, key, stream);
					undo_redo->add_undo_method(animation.ptr(), "audio_track_set_key_stream", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}

				if (name == "start_offset") {
					float value = p_value;

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					float prev = animation->audio_track_get_key_start_offset(track, key);
					undo_redo->add_do_method(animation.ptr(), "audio_track_set_key_start_offset", track, key, value);
					undo_redo->add_undo_method(animation.ptr(), "audio_track_set_key_start_offset", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}

				if (name == "end_offset") {
					float value = p_value;

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					float prev = animation->audio_track_get_key_end_offset(track, key);
					undo_redo->add_do_method(animation.ptr(), "audio_track_set_key_end_offset", track, key, value);
					undo_redo->add_undo_method(animation.ptr(), "audio_track_set_key_end_offset", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}
			} break;
			case Animation::TYPE_ANIMATION: {
				if (name == "animation") {
					StringName anim_name = p_value;

					setting = true;
					undo_redo->create_action(TTR("Anim Change Keyframe Value"), UndoRedo::MERGE_ENDS);
					StringName prev = animation->animation_track_get_key_animation(track, key);
					undo_redo->add_do_method(animation.ptr(), "animation_track_set_key_animation", track, key, anim_name);
					undo_redo->add_undo_method(animation.ptr(), "animation_track_set_key_animation", track, key, prev);
					undo_redo->add_do_method(this, "_update_obj", animation);
					undo_redo->add_undo_method(this, "_update_obj", animation);
					undo_redo->commit_action();

					setting = false;
					return true;
				}
			} break;
		}

		return false;
	}

	bool _get(const StringName &p_name, Variant &r_ret) const {
		int key = animation->track_find_key(track, key_ofs, true);
		ERR_FAIL_COND_V(key == -1, false);

		String name = p_name;
		if (name == "time") {
			r_ret = key_ofs;
			return true;
		}

		if (name == "frame") {
			float fps = animation->get_step();
			if (fps > 0) {
				fps = 1.0 / fps;
			}
			r_ret = key_ofs * fps;
			return true;
		}

		if (name == "easing") {
			r_ret = animation->track_get_key_transition(track, key);
			return true;
		}

		switch (animation->track_get_type(track)) {
			case Animation::TYPE_POSITION_3D:
			case Animation::TYPE_ROTATION_3D:
			case Animation::TYPE_SCALE_3D: {
				if (name == "position" || name == "rotation" || name == "scale") {
					r_ret = animation->track_get_key_value(track, key);
					return true;
				}
			} break;
			case Animation::TYPE_BLEND_SHAPE:
			case Animation::TYPE_VALUE: {
				if (name == "value") {
					r_ret = animation->track_get_key_value(track, key);
					return true;
				}

			} break;
			case Animation::TYPE_METHOD: {
				Dictionary d = animation->track_get_key_value(track, key);

				if (name == "name") {
					ERR_FAIL_COND_V(!d.has("method"), false);
					r_ret = d["method"];
					return true;
				}

				ERR_FAIL_COND_V(!d.has("args"), false);

				Vector<Variant> args = d["args"];

				if (name == "arg_count") {
					r_ret = args.size();
					return true;
				}

				if (name.begins_with("args/")) {
					int idx = name.get_slice("/", 1).to_int();
					ERR_FAIL_INDEX_V(idx, args.size(), false);

					String what = name.get_slice("/", 2);
					if (what == "type") {
						r_ret = args[idx].get_type();
						return true;
					}

					if (what == "value") {
						r_ret = args[idx];
						return true;
					}
				}

			} break;
			case Animation::TYPE_BEZIER: {
				if (name == "value") {
					r_ret = animation->bezier_track_get_key_value(track, key);
					return true;
				}

				if (name == "in_handle") {
					r_ret = animation->bezier_track_get_key_in_handle(track, key);
					return true;
				}

				if (name == "out_handle") {
					r_ret = animation->bezier_track_get_key_out_handle(track, key);
					return true;
				}

				if (name == "handle_mode") {
					r_ret = animation->bezier_track_get_key_handle_mode(track, key);
					return true;
				}

			} break;
			case Animation::TYPE_AUDIO: {
				if (name == "stream") {
					r_ret = animation->audio_track_get_key_stream(track, key);
					return true;
				}

				if (name == "start_offset") {
					r_ret = animation->audio_track_get_key_start_offset(track, key);
					return true;
				}

				if (name == "end_offset") {
					r_ret = animation->audio_track_get_key_end_offset(track, key);
					return true;
				}

			} break;
			case Animation::TYPE_ANIMATION: {
				if (name == "animation") {
					r_ret = animation->animation_track_get_key_animation(track, key);
					return true;
				}

			} break;
		}

		return false;
	}
	void _get_property_list(List<PropertyInfo> *p_list) const {
		if (animation.is_null()) {
			return;
		}

		ERR_FAIL_INDEX(track, animation->get_track_count());
		int key = animation->track_find_key(track, key_ofs, true);
		ERR_FAIL_COND(key == -1);

		if (use_fps && animation->get_step() > 0) {
			float max_frame = animation->get_length() / animation->get_step();
			p_list->push_back(PropertyInfo(Variant::FLOAT, PNAME("frame"), PROPERTY_HINT_RANGE, "0," + rtos(max_frame) + ",1"));
		} else {
			p_list->push_back(PropertyInfo(Variant::FLOAT, PNAME("time"), PROPERTY_HINT_RANGE, "0," + rtos(animation->get_length()) + ",0.01"));
		}

		switch (animation->track_get_type(track)) {
			case Animation::TYPE_POSITION_3D: {
				p_list->push_back(PropertyInfo(Variant::VECTOR3, PNAME("position")));
			} break;
			case Animation::TYPE_ROTATION_3D: {
				p_list->push_back(PropertyInfo(Variant::QUATERNION, PNAME("rotation")));
			} break;
			case Animation::TYPE_SCALE_3D: {
				p_list->push_back(PropertyInfo(Variant::VECTOR3, PNAME("scale")));
			} break;
			case Animation::TYPE_BLEND_SHAPE: {
				p_list->push_back(PropertyInfo(Variant::FLOAT, PNAME("value")));
			} break;
			case Animation::TYPE_VALUE: {
				Variant v = animation->track_get_key_value(track, key);

				if (hint.type != Variant::NIL) {
					PropertyInfo pi = hint;
					pi.name = PNAME("value");
					p_list->push_back(pi);
				} else {
					PropertyHint val_hint = PROPERTY_HINT_NONE;
					String val_hint_string;

					if (v.get_type() == Variant::OBJECT) {
						// Could actually check the object property if exists..? Yes I will!
						Ref<Resource> res = v;
						if (res.is_valid()) {
							val_hint = PROPERTY_HINT_RESOURCE_TYPE;
							val_hint_string = res->get_class();
						}
					}

					if (v.get_type() != Variant::NIL) {
						p_list->push_back(PropertyInfo(v.get_type(), PNAME("value"), val_hint, val_hint_string));
					}
				}

			} break;
			case Animation::TYPE_METHOD: {
				p_list->push_back(PropertyInfo(Variant::STRING_NAME, PNAME("name")));
				p_list->push_back(PropertyInfo(Variant::INT, PNAME("arg_count"), PROPERTY_HINT_RANGE, "0,32,1,or_greater"));

				Dictionary d = animation->track_get_key_value(track, key);
				ERR_FAIL_COND(!d.has("args"));
				Vector<Variant> args = d["args"];
				String vtypes;
				for (int i = 0; i < Variant::VARIANT_MAX; i++) {
					if (i > 0) {
						vtypes += ",";
					}
					vtypes += Variant::get_type_name(Variant::Type(i));
				}

				for (int i = 0; i < args.size(); i++) {
					p_list->push_back(PropertyInfo(Variant::INT, vformat("%s/%d/%s", PNAME("args"), i, PNAME("type")), PROPERTY_HINT_ENUM, vtypes));
					if (args[i].get_type() != Variant::NIL) {
						p_list->push_back(PropertyInfo(args[i].get_type(), vformat("%s/%d/%s", PNAME("args"), i, PNAME("value"))));
					}
				}

			} break;
			case Animation::TYPE_BEZIER: {
				p_list->push_back(PropertyInfo(Variant::FLOAT, PNAME("value")));
				p_list->push_back(PropertyInfo(Variant::VECTOR2, PNAME("in_handle")));
				p_list->push_back(PropertyInfo(Variant::VECTOR2, PNAME("out_handle")));
				p_list->push_back(PropertyInfo(Variant::INT, PNAME("handle_mode"), PROPERTY_HINT_ENUM, "Free,Balanced"));

			} break;
			case Animation::TYPE_AUDIO: {
				p_list->push_back(PropertyInfo(Variant::OBJECT, PNAME("stream"), PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"));
				p_list->push_back(PropertyInfo(Variant::FLOAT, PNAME("start_offset"), PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater"));
				p_list->push_back(PropertyInfo(Variant::FLOAT, PNAME("end_offset"), PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater"));

			} break;
			case Animation::TYPE_ANIMATION: {
				String animations;

				if (root_path && root_path->has_node(animation->track_get_path(track))) {
					AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(root_path->get_node(animation->track_get_path(track)));
					if (ap) {
						List<StringName> anims;
						ap->get_animation_list(&anims);
						for (const StringName &E : anims) {
							if (!animations.is_empty()) {
								animations += ",";
							}

							animations += String(E);
						}
					}
				}

				if (!animations.is_empty()) {
					animations += ",";
				}
				animations += "[stop]";

				p_list->push_back(PropertyInfo(Variant::STRING_NAME, PNAME("animation"), PROPERTY_HINT_ENUM, animations));

			} break;
		}

		if (animation->track_get_type(track) == Animation::TYPE_VALUE) {
			p_list->push_back(PropertyInfo(Variant::FLOAT, PNAME("easing"), PROPERTY_HINT_EXP_EASING));
		}
	}

	UndoRedo *undo_redo = nullptr;
	Ref<Animation> animation;
	int track = -1;
	float key_ofs = 0;
	Node *root_path = nullptr;

	PropertyInfo hint;
	NodePath base;
	bool use_fps = false;

	void notify_change() {
		notify_property_list_changed();
	}

	Node *get_root_path() {
		return root_path;
	}

	void set_use_fps(bool p_enable) {
		use_fps = p_enable;
		notify_property_list_changed();
	}
};

class AnimationMultiTrackKeyEdit : public Object {
	GDCLASS(AnimationMultiTrackKeyEdit, Object);

public:
	bool setting = false;

	bool _hide_script_from_inspector() {
		return true;
	}

	bool _dont_undo_redo() {
		return true;
	}

	static void _bind_methods() {
		ClassDB::bind_method("_update_obj", &AnimationMultiTrackKeyEdit::_update_obj);
		ClassDB::bind_method("_key_ofs_changed", &AnimationMultiTrackKeyEdit::_key_ofs_changed);
		ClassDB::bind_method("_hide_script_from_inspector", &AnimationMultiTrackKeyEdit::_hide_script_from_inspector);
		ClassDB::bind_method("get_root_path", &AnimationMultiTrackKeyEdit::get_root_path);
		ClassDB::bind_method("_dont_undo_redo", &AnimationMultiTrackKeyEdit::_dont_undo_redo);
	}

	void _fix_node_path(Variant &value, NodePath &base) {
		NodePath np = value;

		if (np == NodePath()) {
			return;
		}

		Node *root = EditorNode::get_singleton()->get_tree()->get_root();

		Node *np_node = root->get_node(np);
		ERR_FAIL_COND(!np_node);

		Node *edited_node = root->get_node(base);
		ERR_FAIL_COND(!edited_node);

		value = edited_node->get_path_to(np_node);
	}

	void _update_obj(const Ref<Animation> &p_anim) {
		if (setting || animation != p_anim) {
			return;
		}

		notify_change();
	}

	void _key_ofs_changed(const Ref<Animation> &p_anim, float from, float to) {
		if (animation != p_anim) {
			return;
		}

		for (const KeyValue<int, List<float>> &E : key_ofs_map) {
			int key = 0;
			for (const float &key_ofs : E.value) {
				if (from != key_ofs) {
					key++;
					continue;
				}

				int track = E.key;
				key_ofs_map[track][key] = to;

				if (setting) {
					return;
				}

				notify_change();

				return;
			}
		}
	}

	bool _set(const StringName &p_name, const Variant &p_value) {
		bool update_obj = false;
		bool change_notify_deserved = false;
		for (const KeyValue<int, List<float>> &E : key_ofs_map) {
			int track = E.key;
			for (const float &key_ofs : E.value) {
				int key = animation->track_find_key(track, key_ofs, true);
				ERR_FAIL_COND_V(key == -1, false);

				String name = p_name;
				if (name == "time" || name == "frame") {
					float new_time = p_value;

					if (name == "frame") {
						float fps = animation->get_step();
						if (fps > 0) {
							fps = 1.0 / fps;
						}
						new_time /= fps;
					}

					int existing = animation->track_find_key(track, new_time, true);

					if (!setting) {
						setting = true;
						undo_redo->create_action(TTR("Anim Multi Change Keyframe Time"), UndoRedo::MERGE_ENDS);
					}

					Variant val = animation->track_get_key_value(track, key);
					float trans = animation->track_get_key_transition(track, key);

					undo_redo->add_do_method(animation.ptr(), "track_remove_key", track, key);
					undo_redo->add_do_method(animation.ptr(), "track_insert_key", track, new_time, val, trans);
					undo_redo->add_do_method(this, "_key_ofs_changed", animation, key_ofs, new_time);
					undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", track, new_time);
					undo_redo->add_undo_method(animation.ptr(), "track_insert_key", track, key_ofs, val, trans);
					undo_redo->add_undo_method(this, "_key_ofs_changed", animation, new_time, key_ofs);

					if (existing != -1) {
						Variant v = animation->track_get_key_value(track, existing);
						trans = animation->track_get_key_transition(track, existing);
						undo_redo->add_undo_method(animation.ptr(), "track_insert_key", track, new_time, v, trans);
					}
				} else if (name == "easing") {
					float val = p_value;
					float prev_val = animation->track_get_key_transition(track, key);

					if (!setting) {
						setting = true;
						undo_redo->create_action(TTR("Anim Multi Change Transition"), UndoRedo::MERGE_ENDS);
					}
					undo_redo->add_do_method(animation.ptr(), "track_set_key_transition", track, key, val);
					undo_redo->add_undo_method(animation.ptr(), "track_set_key_transition", track, key, prev_val);
					update_obj = true;
				}

				switch (animation->track_get_type(track)) {
					case Animation::TYPE_POSITION_3D:
					case Animation::TYPE_ROTATION_3D:
					case Animation::TYPE_SCALE_3D: {
						Variant old = animation->track_get_key_value(track, key);
						if (!setting) {
							String chan;
							switch (animation->track_get_type(track)) {
								case Animation::TYPE_POSITION_3D:
									chan = "Position3D";
									break;
								case Animation::TYPE_ROTATION_3D:
									chan = "Rotation3D";
									break;
								case Animation::TYPE_SCALE_3D:
									chan = "Scale3D";
									break;
								default: {
								}
							}

							setting = true;
							undo_redo->create_action(vformat(TTR("Anim Multi Change %s"), chan));
						}
						undo_redo->add_do_method(animation.ptr(), "track_set_key_value", track, key, p_value);
						undo_redo->add_undo_method(animation.ptr(), "track_set_key_value", track, key, old);
						update_obj = true;
					} break;
					case Animation::TYPE_BLEND_SHAPE:
					case Animation::TYPE_VALUE: {
						if (name == "value") {
							Variant value = p_value;

							if (value.get_type() == Variant::NODE_PATH) {
								_fix_node_path(value, base_map[track]);
							}

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							Variant prev = animation->track_get_key_value(track, key);
							undo_redo->add_do_method(animation.ptr(), "track_set_key_value", track, key, value);
							undo_redo->add_undo_method(animation.ptr(), "track_set_key_value", track, key, prev);
							update_obj = true;
						}
					} break;
					case Animation::TYPE_METHOD: {
						Dictionary d_old = animation->track_get_key_value(track, key);
						Dictionary d_new = d_old.duplicate();

						bool mergeable = false;

						if (name == "name") {
							d_new["method"] = p_value;
						} else if (name == "arg_count") {
							Vector<Variant> args = d_old["args"];
							args.resize(p_value);
							d_new["args"] = args;
							change_notify_deserved = true;
						} else if (name.begins_with("args/")) {
							Vector<Variant> args = d_old["args"];
							int idx = name.get_slice("/", 1).to_int();
							ERR_FAIL_INDEX_V(idx, args.size(), false);

							String what = name.get_slice("/", 2);
							if (what == "type") {
								Variant::Type t = Variant::Type(int(p_value));

								if (t != args[idx].get_type()) {
									Callable::CallError err;
									if (Variant::can_convert(args[idx].get_type(), t)) {
										Variant old = args[idx];
										Variant *ptrs[1] = { &old };
										Variant::construct(t, args.write[idx], (const Variant **)ptrs, 1, err);
									} else {
										Variant::construct(t, args.write[idx], nullptr, 0, err);
									}
									change_notify_deserved = true;
									d_new["args"] = args;
								}
							} else if (what == "value") {
								Variant value = p_value;
								if (value.get_type() == Variant::NODE_PATH) {
									_fix_node_path(value, base_map[track]);
								}

								args.write[idx] = value;
								d_new["args"] = args;
								mergeable = true;
							}
						}

						Variant prev = animation->track_get_key_value(track, key);

						if (!setting) {
							if (mergeable) {
								undo_redo->create_action(TTR("Anim Multi Change Call"), UndoRedo::MERGE_ENDS);
							} else {
								undo_redo->create_action(TTR("Anim Multi Change Call"));
							}

							setting = true;
						}

						undo_redo->add_do_method(animation.ptr(), "track_set_key_value", track, key, d_new);
						undo_redo->add_undo_method(animation.ptr(), "track_set_key_value", track, key, d_old);
						update_obj = true;
					} break;
					case Animation::TYPE_BEZIER: {
						if (name == "value") {
							const Variant &value = p_value;

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							float prev = animation->bezier_track_get_key_value(track, key);
							undo_redo->add_do_method(animation.ptr(), "bezier_track_set_key_value", track, key, value);
							undo_redo->add_undo_method(animation.ptr(), "bezier_track_set_key_value", track, key, prev);
							update_obj = true;
						} else if (name == "in_handle") {
							const Variant &value = p_value;

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							Vector2 prev = animation->bezier_track_get_key_in_handle(track, key);
							undo_redo->add_do_method(animation.ptr(), "bezier_track_set_key_in_handle", track, key, value);
							undo_redo->add_undo_method(animation.ptr(), "bezier_track_set_key_in_handle", track, key, prev);
							update_obj = true;
						} else if (name == "out_handle") {
							const Variant &value = p_value;

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							Vector2 prev = animation->bezier_track_get_key_out_handle(track, key);
							undo_redo->add_do_method(animation.ptr(), "bezier_track_set_key_out_handle", track, key, value);
							undo_redo->add_undo_method(animation.ptr(), "bezier_track_set_key_out_handle", track, key, prev);
							update_obj = true;
						} else if (name == "handle_mode") {
							const Variant &value = p_value;

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							int prev = animation->bezier_track_get_key_handle_mode(track, key);
							undo_redo->add_do_method(animation.ptr(), "bezier_track_set_key_handle_mode", track, key, value);
							undo_redo->add_undo_method(animation.ptr(), "bezier_track_set_key_handle_mode", track, key, prev);
							update_obj = true;
						}
					} break;
					case Animation::TYPE_AUDIO: {
						if (name == "stream") {
							Ref<AudioStream> stream = p_value;

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							Ref<Resource> prev = animation->audio_track_get_key_stream(track, key);
							undo_redo->add_do_method(animation.ptr(), "audio_track_set_key_stream", track, key, stream);
							undo_redo->add_undo_method(animation.ptr(), "audio_track_set_key_stream", track, key, prev);
							update_obj = true;
						} else if (name == "start_offset") {
							float value = p_value;

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							float prev = animation->audio_track_get_key_start_offset(track, key);
							undo_redo->add_do_method(animation.ptr(), "audio_track_set_key_start_offset", track, key, value);
							undo_redo->add_undo_method(animation.ptr(), "audio_track_set_key_start_offset", track, key, prev);
							update_obj = true;
						} else if (name == "end_offset") {
							float value = p_value;

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							float prev = animation->audio_track_get_key_end_offset(track, key);
							undo_redo->add_do_method(animation.ptr(), "audio_track_set_key_end_offset", track, key, value);
							undo_redo->add_undo_method(animation.ptr(), "audio_track_set_key_end_offset", track, key, prev);
							update_obj = true;
						}
					} break;
					case Animation::TYPE_ANIMATION: {
						if (name == "animation") {
							StringName anim_name = p_value;

							if (!setting) {
								setting = true;
								undo_redo->create_action(TTR("Anim Multi Change Keyframe Value"), UndoRedo::MERGE_ENDS);
							}
							StringName prev = animation->animation_track_get_key_animation(track, key);
							undo_redo->add_do_method(animation.ptr(), "animation_track_set_key_animation", track, key, anim_name);
							undo_redo->add_undo_method(animation.ptr(), "animation_track_set_key_animation", track, key, prev);
							update_obj = true;
						}
					} break;
				}
			}
		}

		if (setting) {
			if (update_obj) {
				undo_redo->add_do_method(this, "_update_obj", animation);
				undo_redo->add_undo_method(this, "_update_obj", animation);
			}

			undo_redo->commit_action();
			setting = false;

			if (change_notify_deserved) {
				notify_change();
			}

			return true;
		}

		return false;
	}

	bool _get(const StringName &p_name, Variant &r_ret) const {
		for (const KeyValue<int, List<float>> &E : key_ofs_map) {
			int track = E.key;
			for (const float &key_ofs : E.value) {
				int key = animation->track_find_key(track, key_ofs, true);
				ERR_CONTINUE(key == -1);

				String name = p_name;
				if (name == "time") {
					r_ret = key_ofs;
					return true;
				}

				if (name == "frame") {
					float fps = animation->get_step();
					if (fps > 0) {
						fps = 1.0 / fps;
					}
					r_ret = key_ofs * fps;
					return true;
				}

				if (name == "easing") {
					r_ret = animation->track_get_key_transition(track, key);
					return true;
				}

				switch (animation->track_get_type(track)) {
					case Animation::TYPE_POSITION_3D:
					case Animation::TYPE_ROTATION_3D:
					case Animation::TYPE_SCALE_3D: {
						if (name == "position" || name == "rotation" || name == "scale") {
							r_ret = animation->track_get_key_value(track, key);
							return true;
						}

					} break;
					case Animation::TYPE_BLEND_SHAPE:
					case Animation::TYPE_VALUE: {
						if (name == "value") {
							r_ret = animation->track_get_key_value(track, key);
							return true;
						}

					} break;
					case Animation::TYPE_METHOD: {
						Dictionary d = animation->track_get_key_value(track, key);

						if (name == "name") {
							ERR_FAIL_COND_V(!d.has("method"), false);
							r_ret = d["method"];
							return true;
						}

						ERR_FAIL_COND_V(!d.has("args"), false);

						Vector<Variant> args = d["args"];

						if (name == "arg_count") {
							r_ret = args.size();
							return true;
						}

						if (name.begins_with("args/")) {
							int idx = name.get_slice("/", 1).to_int();
							ERR_FAIL_INDEX_V(idx, args.size(), false);

							String what = name.get_slice("/", 2);
							if (what == "type") {
								r_ret = args[idx].get_type();
								return true;
							}

							if (what == "value") {
								r_ret = args[idx];
								return true;
							}
						}

					} break;
					case Animation::TYPE_BEZIER: {
						if (name == "value") {
							r_ret = animation->bezier_track_get_key_value(track, key);
							return true;
						}

						if (name == "in_handle") {
							r_ret = animation->bezier_track_get_key_in_handle(track, key);
							return true;
						}

						if (name == "out_handle") {
							r_ret = animation->bezier_track_get_key_out_handle(track, key);
							return true;
						}

						if (name == "handle_mode") {
							r_ret = animation->bezier_track_get_key_handle_mode(track, key);
							return true;
						}

					} break;
					case Animation::TYPE_AUDIO: {
						if (name == "stream") {
							r_ret = animation->audio_track_get_key_stream(track, key);
							return true;
						}

						if (name == "start_offset") {
							r_ret = animation->audio_track_get_key_start_offset(track, key);
							return true;
						}

						if (name == "end_offset") {
							r_ret = animation->audio_track_get_key_end_offset(track, key);
							return true;
						}

					} break;
					case Animation::TYPE_ANIMATION: {
						if (name == "animation") {
							r_ret = animation->animation_track_get_key_animation(track, key);
							return true;
						}

					} break;
				}
			}
		}

		return false;
	}
	void _get_property_list(List<PropertyInfo> *p_list) const {
		if (animation.is_null()) {
			return;
		}

		int first_track = -1;
		float first_key = -1.0;

		bool show_time = true;
		bool same_track_type = true;
		bool same_key_type = true;
		for (const KeyValue<int, List<float>> &E : key_ofs_map) {
			int track = E.key;
			ERR_FAIL_INDEX(track, animation->get_track_count());

			if (first_track < 0) {
				first_track = track;
			}

			if (show_time && E.value.size() > 1) {
				show_time = false;
			}

			if (same_track_type) {
				if (animation->track_get_type(first_track) != animation->track_get_type(track)) {
					same_track_type = false;
					same_key_type = false;
				}

				for (const float &F : E.value) {
					int key = animation->track_find_key(track, F, true);
					ERR_FAIL_COND(key == -1);
					if (first_key < 0) {
						first_key = key;
					}

					if (animation->track_get_key_value(first_track, first_key).get_type() != animation->track_get_key_value(track, key).get_type()) {
						same_key_type = false;
					}
				}
			}
		}

		if (show_time) {
			if (use_fps && animation->get_step() > 0) {
				float max_frame = animation->get_length() / animation->get_step();
				p_list->push_back(PropertyInfo(Variant::FLOAT, "frame", PROPERTY_HINT_RANGE, "0," + rtos(max_frame) + ",1"));
			} else {
				p_list->push_back(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_RANGE, "0," + rtos(animation->get_length()) + ",0.01"));
			}
		}

		if (same_track_type) {
			switch (animation->track_get_type(first_track)) {
				case Animation::TYPE_POSITION_3D: {
					p_list->push_back(PropertyInfo(Variant::VECTOR3, "position"));
				} break;
				case Animation::TYPE_ROTATION_3D: {
					p_list->push_back(PropertyInfo(Variant::QUATERNION, "scale"));
				} break;
				case Animation::TYPE_SCALE_3D: {
					p_list->push_back(PropertyInfo(Variant::VECTOR3, "scale"));
				} break;
				case Animation::TYPE_BLEND_SHAPE: {
					p_list->push_back(PropertyInfo(Variant::FLOAT, "value"));
				} break;
				case Animation::TYPE_VALUE: {
					if (same_key_type) {
						Variant v = animation->track_get_key_value(first_track, first_key);

						if (hint.type != Variant::NIL) {
							PropertyInfo pi = hint;
							pi.name = "value";
							p_list->push_back(pi);
						} else {
							PropertyHint val_hint = PROPERTY_HINT_NONE;
							String val_hint_string;

							if (v.get_type() == Variant::OBJECT) {
								// Could actually check the object property if exists..? Yes I will!
								Ref<Resource> res = v;
								if (res.is_valid()) {
									val_hint = PROPERTY_HINT_RESOURCE_TYPE;
									val_hint_string = res->get_class();
								}
							}

							if (v.get_type() != Variant::NIL) {
								p_list->push_back(PropertyInfo(v.get_type(), "value", val_hint, val_hint_string));
							}
						}
					}

					p_list->push_back(PropertyInfo(Variant::FLOAT, "easing", PROPERTY_HINT_EXP_EASING));
				} break;
				case Animation::TYPE_METHOD: {
					p_list->push_back(PropertyInfo(Variant::STRING_NAME, "name"));

					p_list->push_back(PropertyInfo(Variant::INT, "arg_count", PROPERTY_HINT_RANGE, "0,32,1,or_greater"));

					Dictionary d = animation->track_get_key_value(first_track, first_key);
					ERR_FAIL_COND(!d.has("args"));
					Vector<Variant> args = d["args"];
					String vtypes;
					for (int i = 0; i < Variant::VARIANT_MAX; i++) {
						if (i > 0) {
							vtypes += ",";
						}
						vtypes += Variant::get_type_name(Variant::Type(i));
					}

					for (int i = 0; i < args.size(); i++) {
						p_list->push_back(PropertyInfo(Variant::INT, "args/" + itos(i) + "/type", PROPERTY_HINT_ENUM, vtypes));
						if (args[i].get_type() != Variant::NIL) {
							p_list->push_back(PropertyInfo(args[i].get_type(), "args/" + itos(i) + "/value"));
						}
					}
				} break;
				case Animation::TYPE_BEZIER: {
					p_list->push_back(PropertyInfo(Variant::FLOAT, "value"));
					p_list->push_back(PropertyInfo(Variant::VECTOR2, "in_handle"));
					p_list->push_back(PropertyInfo(Variant::VECTOR2, "out_handle"));
					p_list->push_back(PropertyInfo(Variant::INT, "handle_mode", PROPERTY_HINT_ENUM, "Free,Balanced"));
				} break;
				case Animation::TYPE_AUDIO: {
					p_list->push_back(PropertyInfo(Variant::OBJECT, "stream", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"));
					p_list->push_back(PropertyInfo(Variant::FLOAT, "start_offset", PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater"));
					p_list->push_back(PropertyInfo(Variant::FLOAT, "end_offset", PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater"));
				} break;
				case Animation::TYPE_ANIMATION: {
					if (key_ofs_map.size() > 1) {
						break;
					}

					String animations;

					if (root_path && root_path->has_node(animation->track_get_path(first_track))) {
						AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(root_path->get_node(animation->track_get_path(first_track)));
						if (ap) {
							List<StringName> anims;
							ap->get_animation_list(&anims);
							for (List<StringName>::Element *G = anims.front(); G; G = G->next()) {
								if (!animations.is_empty()) {
									animations += ",";
								}

								animations += String(G->get());
							}
						}
					}

					if (!animations.is_empty()) {
						animations += ",";
					}
					animations += "[stop]";

					p_list->push_back(PropertyInfo(Variant::STRING_NAME, "animation", PROPERTY_HINT_ENUM, animations));
				} break;
			}
		}
	}

	Ref<Animation> animation;

	RBMap<int, List<float>> key_ofs_map;
	RBMap<int, NodePath> base_map;
	PropertyInfo hint;

	Node *root_path = nullptr;

	bool use_fps = false;

	UndoRedo *undo_redo = nullptr;

	void notify_change() {
		notify_property_list_changed();
	}

	Node *get_root_path() {
		return root_path;
	}

	void set_use_fps(bool p_enable) {
		use_fps = p_enable;
		notify_property_list_changed();
	}
};

void AnimationTimelineEdit::_zoom_changed(double) {
	update();
	play_position->update();
	emit_signal(SNAME("zoom_changed"));
}

float AnimationTimelineEdit::get_zoom_scale() const {
	float zv = zoom->get_max() - zoom->get_value();
	if (zv < 1) {
		zv = 1.0 - zv;
		return Math::pow(1.0f + zv, 8.0f) * 100;
	} else {
		return 1.0 / Math::pow(zv, 8.0f) * 100;
	}
}

void AnimationTimelineEdit::_anim_length_changed(double p_new_len) {
	if (editing) {
		return;
	}

	p_new_len = MAX(0.001, p_new_len);
	if (use_fps && animation->get_step() > 0) {
		p_new_len *= animation->get_step();
	}

	editing = true;
	undo_redo->create_action(TTR("Change Animation Length"));
	undo_redo->add_do_method(animation.ptr(), "set_length", p_new_len);
	undo_redo->add_undo_method(animation.ptr(), "set_length", animation->get_length());
	undo_redo->commit_action();
	editing = false;
	update();

	emit_signal(SNAME("length_changed"), p_new_len);
}

void AnimationTimelineEdit::_anim_loop_pressed() {
	undo_redo->create_action(TTR("Change Animation Loop"));
	switch (animation->get_loop_mode()) {
		case Animation::LOOP_NONE: {
			undo_redo->add_do_method(animation.ptr(), "set_loop_mode", Animation::LOOP_LINEAR);
		} break;
		case Animation::LOOP_LINEAR: {
			undo_redo->add_do_method(animation.ptr(), "set_loop_mode", Animation::LOOP_PINGPONG);
		} break;
		case Animation::LOOP_PINGPONG: {
			undo_redo->add_do_method(animation.ptr(), "set_loop_mode", Animation::LOOP_NONE);
		} break;
		default:
			break;
	}
	undo_redo->add_undo_method(animation.ptr(), "set_loop_mode", animation->get_loop_mode());
	undo_redo->commit_action();
}

int AnimationTimelineEdit::get_buttons_width() const {
	Ref<Texture2D> interp_mode = get_theme_icon(SNAME("TrackContinuous"), SNAME("EditorIcons"));
	Ref<Texture2D> interp_type = get_theme_icon(SNAME("InterpRaw"), SNAME("EditorIcons"));
	Ref<Texture2D> loop_type = get_theme_icon(SNAME("InterpWrapClamp"), SNAME("EditorIcons"));
	Ref<Texture2D> remove_icon = get_theme_icon(SNAME("Remove"), SNAME("EditorIcons"));
	Ref<Texture2D> down_icon = get_theme_icon(SNAME("select_arrow"), SNAME("Tree"));

	int total_w = interp_mode->get_width() + interp_type->get_width() + loop_type->get_width() + remove_icon->get_width();
	total_w += (down_icon->get_width() + 4 * EDSCALE) * 4;

	return total_w;
}

int AnimationTimelineEdit::get_name_limit() const {
	Ref<Texture2D> hsize_icon = get_theme_icon(SNAME("Hsize"), SNAME("EditorIcons"));

	int limit = MAX(name_limit, add_track->get_minimum_size().width + hsize_icon->get_width());

	limit = MIN(limit, get_size().width - get_buttons_width() - 1);

	return limit;
}

void AnimationTimelineEdit::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_THEME_CHANGED: {
			panner->setup((ViewPanner::ControlScheme)EDITOR_GET("editors/panning/animation_editors_panning_scheme").operator int(), ED_GET_SHORTCUT("canvas_item_editor/pan_view"), bool(EditorSettings::get_singleton()->get("editors/panning/simple_panning")));
			add_track->set_icon(get_theme_icon(SNAME("Add"), SNAME("EditorIcons")));
			loop->set_icon(get_theme_icon(SNAME("Loop"), SNAME("EditorIcons")));
			time_icon->set_texture(get_theme_icon(SNAME("Time"), SNAME("EditorIcons")));

			add_track->get_popup()->clear();
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyValue"), SNAME("EditorIcons")), TTR("Property Track"));
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyXPosition"), SNAME("EditorIcons")), TTR("3D Position Track"));
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyXRotation"), SNAME("EditorIcons")), TTR("3D Rotation Track"));
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyXScale"), SNAME("EditorIcons")), TTR("3D Scale Track"));
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyBlendShape"), SNAME("EditorIcons")), TTR("Blend Shape Track"));
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyCall"), SNAME("EditorIcons")), TTR("Call Method Track"));
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyBezier"), SNAME("EditorIcons")), TTR("Bezier Curve Track"));
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyAudio"), SNAME("EditorIcons")), TTR("Audio Playback Track"));
			add_track->get_popup()->add_icon_item(get_theme_icon(SNAME("KeyAnimation"), SNAME("EditorIcons")), TTR("Animation Playback Track"));
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			panner->setup((ViewPanner::ControlScheme)EDITOR_GET("editors/panning/animation_editors_panning_scheme").operator int(), ED_GET_SHORTCUT("canvas_item_editor/pan_view"), bool(EditorSettings::get_singleton()->get("editors/panning/simple_panning")));
		} break;

		case NOTIFICATION_RESIZED: {
			len_hb->set_position(Vector2(get_size().width - get_buttons_width(), 0));
			len_hb->set_size(Size2(get_buttons_width(), get_size().height));
		} break;

		case NOTIFICATION_DRAW: {
			int key_range = get_size().width - get_buttons_width() - get_name_limit();

			if (!animation.is_valid()) {
				return;
			}

			Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
			int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
			Color color = get_theme_color(SNAME("font_color"), SNAME("Label"));

			int zoomw = key_range;
			float scale = get_zoom_scale();
			int h = get_size().height;

			float l = animation->get_length();
			if (l <= 0) {
				l = 0.001; // Avoid crashor.
			}

			Ref<Texture2D> hsize_icon = get_theme_icon(SNAME("Hsize"), SNAME("EditorIcons"));
			hsize_rect = Rect2(get_name_limit() - hsize_icon->get_width() - 2 * EDSCALE, (get_size().height - hsize_icon->get_height()) / 2, hsize_icon->get_width(), hsize_icon->get_height());
			draw_texture(hsize_icon, hsize_rect.position);

			{
				float time_min = 0;
				float time_max = animation->get_length();
				for (int i = 0; i < animation->get_track_count(); i++) {
					if (animation->track_get_key_count(i) > 0) {
						float beg = animation->track_get_key_time(i, 0);

						if (beg < time_min) {
							time_min = beg;
						}

						float end = animation->track_get_key_time(i, animation->track_get_key_count(i) - 1);

						if (end > time_max) {
							time_max = end;
						}
					}
				}

				float extra = (zoomw / scale) * 0.5;

				time_max += extra;
				set_min(time_min);
				set_max(time_max);

				if (zoomw / scale < (time_max - time_min)) {
					hscroll->show();

				} else {
					hscroll->hide();
				}
			}

			set_page(zoomw / scale);

			int end_px = (l - get_value()) * scale;
			int begin_px = -get_value() * scale;
			Color notimecol = get_theme_color(SNAME("dark_color_2"), SNAME("Editor"));
			Color timecolor = color;
			timecolor.a = 0.2;
			Color linecolor = color;
			linecolor.a = 0.2;

			{
				draw_rect(Rect2(Point2(get_name_limit(), 0), Point2(zoomw - 1, h)), notimecol);

				if (begin_px < zoomw && end_px > 0) {
					if (begin_px < 0) {
						begin_px = 0;
					}
					if (end_px > zoomw) {
						end_px = zoomw;
					}

					draw_rect(Rect2(Point2(get_name_limit() + begin_px, 0), Point2(end_px - begin_px - 1, h)), timecolor);
				}
			}

			Color color_time_sec = color;
			Color color_time_dec = color;
			color_time_dec.a *= 0.5;
#define SC_ADJ 100
			int dec = 1;
			int step = 1;
			int decimals = 2;
			bool step_found = false;

			const float period_width = font->get_char_size('.', 0, font_size).width;
			float max_digit_width = font->get_char_size('0', 0, font_size).width;
			for (int i = 1; i <= 9; i++) {
				const float digit_width = font->get_char_size('0' + i, 0, font_size).width;
				max_digit_width = MAX(digit_width, max_digit_width);
			}
			const int max_sc = int(Math::ceil(zoomw / scale));
			const int max_sc_width = String::num(max_sc).length() * max_digit_width;

			while (!step_found) {
				int min = max_sc_width;
				if (decimals > 0) {
					min += period_width + max_digit_width * decimals;
				}

				static const int _multp[3] = { 1, 2, 5 };
				for (int i = 0; i < 3; i++) {
					step = (_multp[i] * dec);
					if (step * scale / SC_ADJ > min) {
						step_found = true;
						break;
					}
				}
				if (step_found) {
					break;
				}
				dec *= 10;
				decimals--;
				if (decimals < 0) {
					decimals = 0;
				}
			}

			if (use_fps) {
				float step_size = animation->get_step();
				if (step_size > 0) {
					int prev_frame_ofs = -10000000;

					for (int i = 0; i < zoomw; i++) {
						float pos = get_value() + double(i) / scale;
						float prev = get_value() + (double(i) - 1.0) / scale;

						int frame = pos / step_size;
						int prev_frame = prev / step_size;

						bool sub = Math::floor(prev) == Math::floor(pos);

						if (frame != prev_frame && i >= prev_frame_ofs) {
							draw_line(Point2(get_name_limit() + i, 0), Point2(get_name_limit() + i, h), linecolor, Math::round(EDSCALE));

							draw_string(font, Point2(get_name_limit() + i + 3 * EDSCALE, (h - font->get_height(font_size)) / 2 + font->get_ascent(font_size)).floor(), itos(frame), HORIZONTAL_ALIGNMENT_LEFT, zoomw - i, font_size, sub ? color_time_dec : color_time_sec);
							prev_frame_ofs = i + font->get_string_size(itos(frame), font_size).x + 5 * EDSCALE;
						}
					}
				}

			} else {
				for (int i = 0; i < zoomw; i++) {
					float pos = get_value() + double(i) / scale;
					float prev = get_value() + (double(i) - 1.0) / scale;

					int sc = int(Math::floor(pos * SC_ADJ));
					int prev_sc = int(Math::floor(prev * SC_ADJ));
					bool sub = (sc % SC_ADJ);

					if ((sc / step) != (prev_sc / step) || (prev_sc < 0 && sc >= 0)) {
						int scd = sc < 0 ? prev_sc : sc;
						draw_line(Point2(get_name_limit() + i, 0), Point2(get_name_limit() + i, h), linecolor, Math::round(EDSCALE));
						draw_string(font, Point2(get_name_limit() + i + 3, (h - font->get_height(font_size)) / 2 + font->get_ascent(font_size)).floor(), String::num((scd - (scd % step)) / double(SC_ADJ), decimals), HORIZONTAL_ALIGNMENT_LEFT, zoomw - i, font_size, sub ? color_time_dec : color_time_sec);
					}
				}
			}

			draw_line(Vector2(0, get_size().height), get_size(), linecolor, Math::round(EDSCALE));
		} break;
	}
}

void AnimationTimelineEdit::set_animation(const Ref<Animation> &p_animation) {
	animation = p_animation;
	if (animation.is_valid()) {
		len_hb->show();
		add_track->show();
		play_position->show();
	} else {
		len_hb->hide();
		add_track->hide();
		play_position->hide();
	}
	update();
	update_values();
}

Size2 AnimationTimelineEdit::get_minimum_size() const {
	Size2 ms = add_track->get_minimum_size();
	Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
	int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
	ms.height = MAX(ms.height, font->get_height(font_size));
	ms.width = get_buttons_width() + add_track->get_minimum_size().width + get_theme_icon(SNAME("Hsize"), SNAME("EditorIcons"))->get_width() + 2;
	return ms;
}

void AnimationTimelineEdit::set_undo_redo(UndoRedo *p_undo_redo) {
	undo_redo = p_undo_redo;
}

void AnimationTimelineEdit::set_zoom(Range *p_zoom) {
	zoom = p_zoom;
	zoom->connect("value_changed", callable_mp(this, &AnimationTimelineEdit::_zoom_changed));
}

void AnimationTimelineEdit::set_track_edit(AnimationTrackEdit *p_track_edit) {
	track_edit = p_track_edit;
}

void AnimationTimelineEdit::set_play_position(float p_pos) {
	play_position_pos = p_pos;
	play_position->update();
}

float AnimationTimelineEdit::get_play_position() const {
	return play_position_pos;
}

void AnimationTimelineEdit::update_play_position() {
	play_position->update();
}

void AnimationTimelineEdit::update_values() {
	if (!animation.is_valid() || editing) {
		return;
	}

	editing = true;
	if (use_fps && animation->get_step() > 0) {
		length->set_value(animation->get_length() / animation->get_step());
		length->set_step(1);
		length->set_tooltip(TTR("Animation length (frames)"));
		time_icon->set_tooltip(TTR("Animation length (frames)"));
	} else {
		length->set_value(animation->get_length());
		length->set_step(0.001);
		length->set_tooltip(TTR("Animation length (seconds)"));
		time_icon->set_tooltip(TTR("Animation length (seconds)"));
	}

	switch (animation->get_loop_mode()) {
		case Animation::LOOP_NONE: {
			loop->set_icon(get_theme_icon(SNAME("Loop"), SNAME("EditorIcons")));
			loop->set_pressed(false);
		} break;
		case Animation::LOOP_LINEAR: {
			loop->set_icon(get_theme_icon(SNAME("Loop"), SNAME("EditorIcons")));
			loop->set_pressed(true);
		} break;
		case Animation::LOOP_PINGPONG: {
			loop->set_icon(get_theme_icon(SNAME("PingPongLoop"), SNAME("EditorIcons")));
			loop->set_pressed(true);
		} break;
		default:
			break;
	}

	editing = false;
}

void AnimationTimelineEdit::_play_position_draw() {
	if (!animation.is_valid() || play_position_pos < 0) {
		return;
	}

	float scale = get_zoom_scale();
	int h = play_position->get_size().height;

	int px = (-get_value() + play_position_pos) * scale + get_name_limit();

	if (px >= get_name_limit() && px < (play_position->get_size().width - get_buttons_width())) {
		Color color = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
		play_position->draw_line(Point2(px, 0), Point2(px, h), color, Math::round(2 * EDSCALE));
		play_position->draw_texture(
				get_theme_icon(SNAME("TimelineIndicator"), SNAME("EditorIcons")),
				Point2(px - get_theme_icon(SNAME("TimelineIndicator"), SNAME("EditorIcons"))->get_width() * 0.5, 0),
				color);
	}
}

void AnimationTimelineEdit::gui_input(const Ref<InputEvent> &p_event) {
	ERR_FAIL_COND(p_event.is_null());

	if (panner->gui_input(p_event)) {
		accept_event();
		return;
	}

	const Ref<InputEventMouseButton> mb = p_event;

	if (mb.is_valid() && mb->is_pressed() && mb->is_alt_pressed() && mb->get_button_index() == MouseButton::WHEEL_UP) {
		if (track_edit) {
			track_edit->get_editor()->goto_prev_step(true);
		}
		accept_event();
	}

	if (mb.is_valid() && mb->is_pressed() && mb->is_alt_pressed() && mb->get_button_index() == MouseButton::WHEEL_DOWN) {
		if (track_edit) {
			track_edit->get_editor()->goto_next_step(true);
		}
		accept_event();
	}

	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT && hsize_rect.has_point(mb->get_position())) {
		dragging_hsize = true;
		dragging_hsize_from = mb->get_position().x;
		dragging_hsize_at = name_limit;
	}

	if (mb.is_valid() && !mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT && dragging_hsize) {
		dragging_hsize = false;
	}
	if (mb.is_valid() && mb->get_position().x > get_name_limit() && mb->get_position().x < (get_size().width - get_buttons_width())) {
		if (!panner->is_panning() && mb->get_button_index() == MouseButton::LEFT) {
			int x = mb->get_position().x - get_name_limit();

			float ofs = x / get_zoom_scale() + get_value();
			emit_signal(SNAME("timeline_changed"), ofs, false, mb->is_alt_pressed());
			dragging_timeline = true;
		}
	}

	if (dragging_timeline && mb.is_valid() && mb->get_button_index() == MouseButton::LEFT && !mb->is_pressed()) {
		dragging_timeline = false;
	}

	Ref<InputEventMouseMotion> mm = p_event;

	if (mm.is_valid()) {
		if (dragging_hsize) {
			int ofs = mm->get_position().x - dragging_hsize_from;
			name_limit = dragging_hsize_at + ofs;
			update();
			emit_signal(SNAME("name_limit_changed"));
			play_position->update();
		}
		if (dragging_timeline) {
			int x = mm->get_position().x - get_name_limit();
			float ofs = x / get_zoom_scale() + get_value();
			emit_signal(SNAME("timeline_changed"), ofs, false, mm->is_alt_pressed());
		}
	}
}

Control::CursorShape AnimationTimelineEdit::get_cursor_shape(const Point2 &p_pos) const {
	if (dragging_hsize || hsize_rect.has_point(p_pos)) {
		// Indicate that the track name column's width can be adjusted
		return Control::CURSOR_HSIZE;
	} else {
		return get_default_cursor_shape();
	}
}

void AnimationTimelineEdit::_scroll_callback(Vector2 p_scroll_vec, bool p_alt) {
	// Timeline has no vertical scroll, so we change it to horizontal.
	p_scroll_vec.x += p_scroll_vec.y;
	_pan_callback(-p_scroll_vec * 32);
}

void AnimationTimelineEdit::_pan_callback(Vector2 p_scroll_vec) {
	set_value(get_value() - p_scroll_vec.x / get_zoom_scale());
}

void AnimationTimelineEdit::_zoom_callback(Vector2 p_scroll_vec, Vector2 p_origin, bool p_alt) {
	double new_zoom_value;
	double current_zoom_value = get_zoom()->get_value();
	if (current_zoom_value <= 0.1) {
		new_zoom_value = MAX(0.01, current_zoom_value - 0.01 * SIGN(p_scroll_vec.y));
	} else {
		new_zoom_value = p_scroll_vec.y > 0 ? MAX(0.01, current_zoom_value / 1.05) : current_zoom_value * 1.05;
	}
	get_zoom()->set_value(new_zoom_value);
}

void AnimationTimelineEdit::set_use_fps(bool p_use_fps) {
	use_fps = p_use_fps;
	update_values();
	update();
}

bool AnimationTimelineEdit::is_using_fps() const {
	return use_fps;
}

void AnimationTimelineEdit::set_hscroll(HScrollBar *p_hscroll) {
	hscroll = p_hscroll;
}

void AnimationTimelineEdit::_track_added(int p_track) {
	emit_signal(SNAME("track_added"), p_track);
}

void AnimationTimelineEdit::_bind_methods() {
	ADD_SIGNAL(MethodInfo("zoom_changed"));
	ADD_SIGNAL(MethodInfo("name_limit_changed"));
	ADD_SIGNAL(MethodInfo("timeline_changed", PropertyInfo(Variant::FLOAT, "position"), PropertyInfo(Variant::BOOL, "drag"), PropertyInfo(Variant::BOOL, "timeline_only")));
	ADD_SIGNAL(MethodInfo("track_added", PropertyInfo(Variant::INT, "track")));
	ADD_SIGNAL(MethodInfo("length_changed", PropertyInfo(Variant::FLOAT, "size")));
}

AnimationTimelineEdit::AnimationTimelineEdit() {
	name_limit = 150 * EDSCALE;

	play_position = memnew(Control);
	play_position->set_mouse_filter(MOUSE_FILTER_PASS);
	add_child(play_position);
	play_position->set_anchors_and_offsets_preset(PRESET_WIDE);
	play_position->connect("draw", callable_mp(this, &AnimationTimelineEdit::_play_position_draw));

	add_track = memnew(MenuButton);
	add_track->set_position(Vector2(0, 0));
	add_child(add_track);
	add_track->set_text(TTR("Add Track"));

	len_hb = memnew(HBoxContainer);

	Control *expander = memnew(Control);
	expander->set_h_size_flags(SIZE_EXPAND_FILL);
	len_hb->add_child(expander);
	time_icon = memnew(TextureRect);
	time_icon->set_v_size_flags(SIZE_SHRINK_CENTER);
	time_icon->set_tooltip(TTR("Animation length (seconds)"));
	len_hb->add_child(time_icon);
	length = memnew(EditorSpinSlider);
	length->set_min(0.001);
	length->set_max(36000);
	length->set_step(0.001);
	length->set_allow_greater(true);
	length->set_custom_minimum_size(Vector2(70 * EDSCALE, 0));
	length->set_hide_slider(true);
	length->set_tooltip(TTR("Animation length (seconds)"));
	length->connect("value_changed", callable_mp(this, &AnimationTimelineEdit::_anim_length_changed));
	len_hb->add_child(length);
	loop = memnew(Button);
	loop->set_flat(true);
	loop->set_tooltip(TTR("Animation Looping"));
	loop->connect("pressed", callable_mp(this, &AnimationTimelineEdit::_anim_loop_pressed));
	loop->set_toggle_mode(true);
	len_hb->add_child(loop);
	add_child(len_hb);

	add_track->hide();
	add_track->get_popup()->connect("index_pressed", callable_mp(this, &AnimationTimelineEdit::_track_added));
	len_hb->hide();

	panner.instantiate();
	panner->set_callbacks(callable_mp(this, &AnimationTimelineEdit::_scroll_callback), callable_mp(this, &AnimationTimelineEdit::_pan_callback), callable_mp(this, &AnimationTimelineEdit::_zoom_callback));

	set_layout_direction(Control::LAYOUT_DIRECTION_LTR);
}

////////////////////////////////////

void AnimationTrackEdit::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (animation.is_null()) {
				return;
			}
			ERR_FAIL_INDEX(track, animation->get_track_count());

			type_icon = _get_key_type_icon();
			selected_icon = get_theme_icon(SNAME("KeySelected"), SNAME("EditorIcons"));
		} break;

		case NOTIFICATION_DRAW: {
			if (animation.is_null()) {
				return;
			}
			ERR_FAIL_INDEX(track, animation->get_track_count());

			int limit = timeline->get_name_limit();

			if (track % 2 == 1) {
				// Draw a background over odd lines to make long lists of tracks easier to read.
				draw_rect(Rect2(Point2(1 * EDSCALE, 0), get_size() - Size2(1 * EDSCALE, 0)), Color(0.5, 0.5, 0.5, 0.05));
			}

			if (hovered) {
				// Draw hover feedback.
				draw_rect(Rect2(Point2(1 * EDSCALE, 0), get_size() - Size2(1 * EDSCALE, 0)), Color(0.5, 0.5, 0.5, 0.1));
			}

			if (has_focus()) {
				Color accent = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
				accent.a *= 0.7;
				// Offside so the horizontal sides aren't cutoff.
				draw_style_box(get_theme_stylebox(SNAME("Focus"), SNAME("EditorStyles")), Rect2(Point2(1 * EDSCALE, 0), get_size() - Size2(1 * EDSCALE, 0)));
			}

			Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
			int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
			Color color = get_theme_color(SNAME("font_color"), SNAME("Label"));
			int hsep = get_theme_constant(SNAME("h_separation"), SNAME("ItemList"));
			Color linecolor = color;
			linecolor.a = 0.2;

			// NAMES AND ICONS //

			{
				Ref<Texture2D> check = animation->track_is_enabled(track) ? get_theme_icon(SNAME("checked"), SNAME("CheckBox")) : get_theme_icon(SNAME("unchecked"), SNAME("CheckBox"));

				int ofs = in_group ? check->get_width() : 0; // Not the best reference for margin but..

				check_rect = Rect2(Point2(ofs, int(get_size().height - check->get_height()) / 2), check->get_size());
				draw_texture(check, check_rect.position);
				ofs += check->get_width() + hsep;

				Ref<Texture2D> type_icon = _get_key_type_icon();
				draw_texture(type_icon, Point2(ofs, int(get_size().height - type_icon->get_height()) / 2));
				ofs += type_icon->get_width() + hsep;

				NodePath path = animation->track_get_path(track);
				Node *node = nullptr;
				if (root && root->has_node(path)) {
					node = root->get_node(path);
				}

				String text;
				Color text_color = color;
				if (node && EditorNode::get_singleton()->get_editor_selection()->is_selected(node)) {
					text_color = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
				}

				if (in_group) {
					if (animation->track_get_type(track) == Animation::TYPE_METHOD) {
						text = TTR("Functions:");
					} else if (animation->track_get_type(track) == Animation::TYPE_AUDIO) {
						text = TTR("Audio Clips:");
					} else if (animation->track_get_type(track) == Animation::TYPE_ANIMATION) {
						text = TTR("Anim Clips:");
					} else {
						text += path.get_concatenated_subnames();
					}
					text_color.a *= 0.7;
				} else if (node) {
					Ref<Texture2D> icon = EditorNode::get_singleton()->get_object_icon(node, "Node");

					draw_texture(icon, Point2(ofs, int(get_size().height - icon->get_height()) / 2));
					icon_cache = icon;

					text = String() + node->get_name() + ":" + path.get_concatenated_subnames();
					ofs += hsep;
					ofs += icon->get_width();

				} else {
					icon_cache = type_icon;

					text = path;
				}

				path_cache = text;

				path_rect = Rect2(ofs, 0, limit - ofs - hsep, get_size().height);

				Vector2 string_pos = Point2(ofs, (get_size().height - font->get_height(font_size)) / 2 + font->get_ascent(font_size));
				string_pos = string_pos.floor();
				draw_string(font, string_pos, text, HORIZONTAL_ALIGNMENT_LEFT, limit - ofs - hsep, font_size, text_color);

				draw_line(Point2(limit, 0), Point2(limit, get_size().height), linecolor, Math::round(EDSCALE));
			}

			// KEYFRAMES //

			draw_bg(limit, get_size().width - timeline->get_buttons_width());

			{
				float scale = timeline->get_zoom_scale();
				int limit_end = get_size().width - timeline->get_buttons_width();

				for (int i = 0; i < animation->track_get_key_count(track); i++) {
					float offset = animation->track_get_key_time(track, i) - timeline->get_value();
					if (editor->is_key_selected(track, i) && editor->is_moving_selection()) {
						offset = editor->snap_time(offset + editor->get_moving_selection_offset(), true);
					}
					offset = offset * scale + limit;
					if (i < animation->track_get_key_count(track) - 1) {
						float offset_n = animation->track_get_key_time(track, i + 1) - timeline->get_value();
						if (editor->is_key_selected(track, i + 1) && editor->is_moving_selection()) {
							offset_n = editor->snap_time(offset_n + editor->get_moving_selection_offset());
						}
						offset_n = offset_n * scale + limit;

						draw_key_link(i, scale, int(offset), int(offset_n), limit, limit_end);
					}

					draw_key(i, scale, int(offset), editor->is_key_selected(track, i), limit, limit_end);
				}
			}

			draw_fg(limit, get_size().width - timeline->get_buttons_width());

			// BUTTONS //

			{
				Ref<Texture2D> wrap_icon[2] = {
					get_theme_icon(SNAME("InterpWrapClamp"), SNAME("EditorIcons")),
					get_theme_icon(SNAME("InterpWrapLoop"), SNAME("EditorIcons")),
				};

				Ref<Texture2D> interp_icon[3] = {
					get_theme_icon(SNAME("InterpRaw"), SNAME("EditorIcons")),
					get_theme_icon(SNAME("InterpLinear"), SNAME("EditorIcons")),
					get_theme_icon(SNAME("InterpCubic"), SNAME("EditorIcons"))
				};
				Ref<Texture2D> cont_icon[4] = {
					get_theme_icon(SNAME("TrackContinuous"), SNAME("EditorIcons")),
					get_theme_icon(SNAME("TrackDiscrete"), SNAME("EditorIcons")),
					get_theme_icon(SNAME("TrackTrigger"), SNAME("EditorIcons")),
					get_theme_icon(SNAME("TrackCapture"), SNAME("EditorIcons"))
				};

				int ofs = get_size().width - timeline->get_buttons_width();

				Ref<Texture2D> down_icon = get_theme_icon(SNAME("select_arrow"), SNAME("Tree"));

				draw_line(Point2(ofs, 0), Point2(ofs, get_size().height), linecolor, Math::round(EDSCALE));

				ofs += hsep;
				{
					// Callmode.

					Animation::UpdateMode update_mode;

					if (animation->track_get_type(track) == Animation::TYPE_VALUE) {
						update_mode = animation->value_track_get_update_mode(track);
					} else {
						update_mode = Animation::UPDATE_CONTINUOUS;
					}

					Ref<Texture2D> update_icon = cont_icon[update_mode];

					update_mode_rect.position.x = ofs;
					update_mode_rect.position.y = int(get_size().height - update_icon->get_height()) / 2;
					update_mode_rect.size = update_icon->get_size();

					if (!animation->track_is_compressed(track) && animation->track_get_type(track) == Animation::TYPE_VALUE) {
						draw_texture(update_icon, update_mode_rect.position);
					}
					// Make it easier to click.
					update_mode_rect.position.y = 0;
					update_mode_rect.size.y = get_size().height;

					ofs += update_icon->get_width() + hsep / 2;
					update_mode_rect.size.x += hsep / 2;

					if (animation->track_get_type(track) == Animation::TYPE_VALUE) {
						draw_texture(down_icon, Vector2(ofs, int(get_size().height - down_icon->get_height()) / 2));
						update_mode_rect.size.x += down_icon->get_width();
					} else if (animation->track_get_type(track) == Animation::TYPE_BEZIER) {
						Ref<Texture2D> bezier_icon = get_theme_icon(SNAME("EditBezier"), SNAME("EditorIcons"));
						update_mode_rect.size.x += down_icon->get_width();

						update_mode_rect = Rect2();
					} else {
						update_mode_rect = Rect2();
					}

					ofs += down_icon->get_width();
					draw_line(Point2(ofs + hsep * 0.5, 0), Point2(ofs + hsep * 0.5, get_size().height), linecolor, Math::round(EDSCALE));
					ofs += hsep;
				}

				{
					// Interp.

					Animation::InterpolationType interp_mode = animation->track_get_interpolation_type(track);

					Ref<Texture2D> icon = interp_icon[interp_mode];

					interp_mode_rect.position.x = ofs;
					interp_mode_rect.position.y = int(get_size().height - icon->get_height()) / 2;
					interp_mode_rect.size = icon->get_size();

					if (!animation->track_is_compressed(track) && (animation->track_get_type(track) == Animation::TYPE_VALUE || animation->track_get_type(track) == Animation::TYPE_BLEND_SHAPE || animation->track_get_type(track) == Animation::TYPE_POSITION_3D || animation->track_get_type(track) == Animation::TYPE_SCALE_3D || animation->track_get_type(track) == Animation::TYPE_ROTATION_3D)) {
						draw_texture(icon, interp_mode_rect.position);
					}
					// Make it easier to click.
					interp_mode_rect.position.y = 0;
					interp_mode_rect.size.y = get_size().height;

					ofs += icon->get_width() + hsep / 2;
					interp_mode_rect.size.x += hsep / 2;

					if (!animation->track_is_compressed(track) && (animation->track_get_type(track) == Animation::TYPE_VALUE || animation->track_get_type(track) == Animation::TYPE_BLEND_SHAPE || animation->track_get_type(track) == Animation::TYPE_POSITION_3D || animation->track_get_type(track) == Animation::TYPE_SCALE_3D || animation->track_get_type(track) == Animation::TYPE_ROTATION_3D)) {
						draw_texture(down_icon, Vector2(ofs, int(get_size().height - down_icon->get_height()) / 2));
						interp_mode_rect.size.x += down_icon->get_width();
					} else {
						interp_mode_rect = Rect2();
					}

					ofs += down_icon->get_width();
					draw_line(Point2(ofs + hsep * 0.5, 0), Point2(ofs + hsep * 0.5, get_size().height), linecolor, Math::round(EDSCALE));
					ofs += hsep;
				}

				{
					// Loop.

					bool loop_wrap = animation->track_get_interpolation_loop_wrap(track);

					Ref<Texture2D> icon = wrap_icon[loop_wrap ? 1 : 0];

					loop_wrap_rect.position.x = ofs;
					loop_wrap_rect.position.y = int(get_size().height - icon->get_height()) / 2;
					loop_wrap_rect.size = icon->get_size();

					if (!animation->track_is_compressed(track) && (animation->track_get_type(track) == Animation::TYPE_VALUE || animation->track_get_type(track) == Animation::TYPE_BLEND_SHAPE || animation->track_get_type(track) == Animation::TYPE_POSITION_3D || animation->track_get_type(track) == Animation::TYPE_SCALE_3D || animation->track_get_type(track) == Animation::TYPE_ROTATION_3D)) {
						draw_texture(icon, loop_wrap_rect.position);
					}

					loop_wrap_rect.position.y = 0;
					loop_wrap_rect.size.y = get_size().height;

					ofs += icon->get_width() + hsep / 2;
					loop_wrap_rect.size.x += hsep / 2;

					if (!animation->track_is_compressed(track) && (animation->track_get_type(track) == Animation::TYPE_VALUE || animation->track_get_type(track) == Animation::TYPE_BLEND_SHAPE || animation->track_get_type(track) == Animation::TYPE_POSITION_3D || animation->track_get_type(track) == Animation::TYPE_SCALE_3D || animation->track_get_type(track) == Animation::TYPE_ROTATION_3D)) {
						draw_texture(down_icon, Vector2(ofs, int(get_size().height - down_icon->get_height()) / 2));
						loop_wrap_rect.size.x += down_icon->get_width();
					} else {
						loop_wrap_rect = Rect2();
					}

					ofs += down_icon->get_width();
					draw_line(Point2(ofs + hsep * 0.5, 0), Point2(ofs + hsep * 0.5, get_size().height), linecolor, Math::round(EDSCALE));
					ofs += hsep;
				}

				{
					// Erase.

					Ref<Texture2D> icon = get_theme_icon(animation->track_is_compressed(track) ? SNAME("Lock") : SNAME("Remove"), SNAME("EditorIcons"));

					remove_rect.position.x = ofs + ((get_size().width - ofs) - icon->get_width());
					remove_rect.position.y = int(get_size().height - icon->get_height()) / 2;
					remove_rect.size = icon->get_size();

					draw_texture(icon, remove_rect.position);
				}
			}

			if (in_group) {
				draw_line(Vector2(timeline->get_name_limit(), get_size().height), get_size(), linecolor, Math::round(EDSCALE));
			} else {
				draw_line(Vector2(0, get_size().height), get_size(), linecolor, Math::round(EDSCALE));
			}

			if (dropping_at != 0) {
				Color drop_color = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
				if (dropping_at < 0) {
					draw_line(Vector2(0, 0), Vector2(get_size().width, 0), drop_color, Math::round(EDSCALE));
				} else {
					draw_line(Vector2(0, get_size().height), get_size(), drop_color, Math::round(EDSCALE));
				}
			}
		} break;

		case NOTIFICATION_MOUSE_ENTER:
			hovered = true;
			update();
			break;
		case NOTIFICATION_MOUSE_EXIT:
			hovered = false;
			// When the mouse cursor exits the track, we're no longer hovering any keyframe.
			hovering_key_idx = -1;
			update();
			[[fallthrough]];
		case NOTIFICATION_DRAG_END: {
			cancel_drop();
		} break;
	}
}

int AnimationTrackEdit::get_key_height() const {
	if (!animation.is_valid()) {
		return 0;
	}

	return type_icon->get_height();
}

Rect2 AnimationTrackEdit::get_key_rect(int p_index, float p_pixels_sec) {
	if (!animation.is_valid()) {
		return Rect2();
	}
	Rect2 rect = Rect2(-type_icon->get_width() / 2, 0, type_icon->get_width(), get_size().height);

	// Make it a big easier to click.
	rect.position.x -= rect.size.x * 0.5;
	rect.size.x *= 2;
	return rect;
}

bool AnimationTrackEdit::is_key_selectable_by_distance() const {
	return true;
}

void AnimationTrackEdit::draw_key_link(int p_index, float p_pixels_sec, int p_x, int p_next_x, int p_clip_left, int p_clip_right) {
	if (p_next_x < p_clip_left) {
		return;
	}
	if (p_x > p_clip_right) {
		return;
	}

	Variant current = animation->track_get_key_value(get_track(), p_index);
	Variant next = animation->track_get_key_value(get_track(), p_index + 1);
	if (current != next) {
		return;
	}

	Color color = get_theme_color(SNAME("font_color"), SNAME("Label"));
	color.a = 0.5;

	int from_x = MAX(p_x, p_clip_left);
	int to_x = MIN(p_next_x, p_clip_right);

	draw_line(Point2(from_x + 1, get_size().height / 2), Point2(to_x, get_size().height / 2), color, Math::round(2 * EDSCALE));
}

void AnimationTrackEdit::draw_key(int p_index, float p_pixels_sec, int p_x, bool p_selected, int p_clip_left, int p_clip_right) {
	if (!animation.is_valid()) {
		return;
	}

	if (p_x < p_clip_left || p_x > p_clip_right) {
		return;
	}

	Ref<Texture2D> icon_to_draw = p_selected ? selected_icon : type_icon;

	if (animation->track_get_type(track) == Animation::TYPE_VALUE && !Math::is_equal_approx(animation->track_get_key_transition(track, p_index), real_t(1.0))) {
		// Use a different icon for keys with non-linear easing.
		icon_to_draw = get_theme_icon(p_selected ? SNAME("KeyEasedSelected") : SNAME("KeyValueEased"), SNAME("EditorIcons"));
	}

	// Override type icon for invalid value keys, unless selected.
	if (!p_selected && animation->track_get_type(track) == Animation::TYPE_VALUE) {
		const Variant &v = animation->track_get_key_value(track, p_index);
		Variant::Type valid_type = Variant::NIL;
		if (!_is_value_key_valid(v, valid_type)) {
			icon_to_draw = get_theme_icon(SNAME("KeyInvalid"), SNAME("EditorIcons"));
		}
	}

	Vector2 ofs(p_x - icon_to_draw->get_width() / 2, int(get_size().height - icon_to_draw->get_height()) / 2);

	if (animation->track_get_type(track) == Animation::TYPE_METHOD) {
		Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
		int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
		Color color = get_theme_color(SNAME("font_color"), SNAME("Label"));
		color.a = 0.5;

		Dictionary d = animation->track_get_key_value(track, p_index);
		String text;

		if (d.has("method")) {
			text += String(d["method"]);
		}
		text += "(";
		Vector<Variant> args;
		if (d.has("args")) {
			args = d["args"];
		}
		for (int i = 0; i < args.size(); i++) {
			if (i > 0) {
				text += ", ";
			}
			text += String(args[i]);
		}
		text += ")";

		int limit = MAX(0, p_clip_right - p_x - icon_to_draw->get_width());
		if (limit > 0) {
			draw_string(font, Vector2(p_x + icon_to_draw->get_width(), int(get_size().height - font->get_height(font_size)) / 2 + font->get_ascent(font_size)), text, HORIZONTAL_ALIGNMENT_LEFT, limit, font_size, color);
		}
	}

	// Use a different color for the currently hovered key.
	// The color multiplier is chosen to work with both dark and light editor themes,
	// and on both unselected and selected key icons.
	draw_texture(
			icon_to_draw,
			ofs,
			p_index == hovering_key_idx ? get_theme_color(SNAME("folder_icon_modulate"), SNAME("FileDialog")) : Color(1, 1, 1));
}

// Helper.
void AnimationTrackEdit::draw_rect_clipped(const Rect2 &p_rect, const Color &p_color, bool p_filled) {
	int clip_left = timeline->get_name_limit();
	int clip_right = get_size().width - timeline->get_buttons_width();

	if (p_rect.position.x > clip_right) {
		return;
	}
	if (p_rect.position.x + p_rect.size.x < clip_left) {
		return;
	}
	Rect2 clip = Rect2(clip_left, 0, clip_right - clip_left, get_size().height);
	draw_rect(clip.intersection(p_rect), p_color, p_filled);
}

void AnimationTrackEdit::draw_bg(int p_clip_left, int p_clip_right) {
}

void AnimationTrackEdit::draw_fg(int p_clip_left, int p_clip_right) {
}

void AnimationTrackEdit::draw_texture_region_clipped(const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Rect2 &p_region) {
	int clip_left = timeline->get_name_limit();
	int clip_right = get_size().width - timeline->get_buttons_width();

	// Clip left and right.
	if (clip_left > p_rect.position.x + p_rect.size.x) {
		return;
	}
	if (clip_right < p_rect.position.x) {
		return;
	}

	Rect2 rect = p_rect;
	Rect2 region = p_region;

	if (clip_left > rect.position.x) {
		int rect_pixels = (clip_left - rect.position.x);
		int region_pixels = rect_pixels * region.size.x / rect.size.x;

		rect.position.x += rect_pixels;
		rect.size.x -= rect_pixels;

		region.position.x += region_pixels;
		region.size.x -= region_pixels;
	}

	if (clip_right < rect.position.x + rect.size.x) {
		int rect_pixels = rect.position.x + rect.size.x - clip_right;
		int region_pixels = rect_pixels * region.size.x / rect.size.x;

		rect.size.x -= rect_pixels;
		region.size.x -= region_pixels;
	}

	draw_texture_rect_region(p_texture, rect, region);
}

int AnimationTrackEdit::get_track() const {
	return track;
}

Ref<Animation> AnimationTrackEdit::get_animation() const {
	return animation;
}

void AnimationTrackEdit::set_animation_and_track(const Ref<Animation> &p_animation, int p_track) {
	animation = p_animation;
	track = p_track;
	update();

	ERR_FAIL_INDEX(track, animation->get_track_count());

	node_path = animation->track_get_path(p_track);
	type_icon = _get_key_type_icon();
	selected_icon = get_theme_icon(SNAME("KeySelected"), SNAME("EditorIcons"));
}

NodePath AnimationTrackEdit::get_path() const {
	return node_path;
}

Size2 AnimationTrackEdit::get_minimum_size() const {
	Ref<Texture2D> texture = get_theme_icon(SNAME("Object"), SNAME("EditorIcons"));
	Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
	int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
	int separation = get_theme_constant(SNAME("v_separation"), SNAME("ItemList"));

	int max_h = MAX(texture->get_height(), font->get_height(font_size));
	max_h = MAX(max_h, get_key_height());

	return Vector2(1, max_h + separation);
}

void AnimationTrackEdit::set_undo_redo(UndoRedo *p_undo_redo) {
	undo_redo = p_undo_redo;
}

void AnimationTrackEdit::set_timeline(AnimationTimelineEdit *p_timeline) {
	timeline = p_timeline;
	timeline->set_track_edit(this);
	timeline->connect("zoom_changed", callable_mp(this, &AnimationTrackEdit::_zoom_changed));
	timeline->connect("name_limit_changed", callable_mp(this, &AnimationTrackEdit::_zoom_changed));
}

void AnimationTrackEdit::set_editor(AnimationTrackEditor *p_editor) {
	editor = p_editor;
}

void AnimationTrackEdit::_play_position_draw() {
	if (!animation.is_valid() || play_position_pos < 0) {
		return;
	}

	float scale = timeline->get_zoom_scale();
	int h = get_size().height;

	int px = (-timeline->get_value() + play_position_pos) * scale + timeline->get_name_limit();

	if (px >= timeline->get_name_limit() && px < (get_size().width - timeline->get_buttons_width())) {
		Color color = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
		play_position->draw_line(Point2(px, 0), Point2(px, h), color, Math::round(2 * EDSCALE));
	}
}

void AnimationTrackEdit::set_play_position(float p_pos) {
	play_position_pos = p_pos;
	play_position->update();
}

void AnimationTrackEdit::update_play_position() {
	play_position->update();
}

void AnimationTrackEdit::set_root(Node *p_root) {
	root = p_root;
}

void AnimationTrackEdit::_zoom_changed() {
	update();
	play_position->update();
}

void AnimationTrackEdit::_path_submitted(const String &p_text) {
	undo_redo->create_action(TTR("Change Track Path"));
	undo_redo->add_do_method(animation.ptr(), "track_set_path", track, p_text);
	undo_redo->add_undo_method(animation.ptr(), "track_set_path", track, animation->track_get_path(track));
	undo_redo->commit_action();
	path_popup->hide();
}

bool AnimationTrackEdit::_is_value_key_valid(const Variant &p_key_value, Variant::Type &r_valid_type) const {
	if (root == nullptr) {
		return false;
	}

	Ref<Resource> res;
	Vector<StringName> leftover_path;
	Node *node = root->get_node_and_resource(animation->track_get_path(track), res, leftover_path);

	Object *obj = nullptr;
	if (res.is_valid()) {
		obj = res.ptr();
	} else if (node) {
		obj = node;
	}

	bool prop_exists = false;
	if (obj) {
		r_valid_type = obj->get_static_property_type_indexed(leftover_path, &prop_exists);
	}

	return (!prop_exists || Variant::can_convert(p_key_value.get_type(), r_valid_type));
}

Ref<Texture2D> AnimationTrackEdit::_get_key_type_icon() const {
	const Ref<Texture2D> type_icons[9] = {
		get_theme_icon(SNAME("KeyValue"), SNAME("EditorIcons")),
		get_theme_icon(SNAME("KeyTrackPosition"), SNAME("EditorIcons")),
		get_theme_icon(SNAME("KeyTrackRotation"), SNAME("EditorIcons")),
		get_theme_icon(SNAME("KeyTrackScale"), SNAME("EditorIcons")),
		get_theme_icon(SNAME("KeyTrackBlendShape"), SNAME("EditorIcons")),
		get_theme_icon(SNAME("KeyCall"), SNAME("EditorIcons")),
		get_theme_icon(SNAME("KeyBezier"), SNAME("EditorIcons")),
		get_theme_icon(SNAME("KeyAudio"), SNAME("EditorIcons")),
		get_theme_icon(SNAME("KeyAnimation"), SNAME("EditorIcons"))
	};
	return type_icons[animation->track_get_type(track)];
}

String AnimationTrackEdit::get_tooltip(const Point2 &p_pos) const {
	if (check_rect.has_point(p_pos)) {
		return TTR("Toggle this track on/off.");
	}

	// Don't overlap track keys if they start at 0.
	if (path_rect.has_point(p_pos + Size2(type_icon->get_width(), 0))) {
		return animation->track_get_path(track);
	}

	if (update_mode_rect.has_point(p_pos)) {
		return TTR("Update Mode (How this property is set)");
	}

	if (interp_mode_rect.has_point(p_pos)) {
		return TTR("Interpolation Mode");
	}

	if (loop_wrap_rect.has_point(p_pos)) {
		return TTR("Loop Wrap Mode (Interpolate end with beginning on loop)");
	}

	if (remove_rect.has_point(p_pos)) {
		return TTR("Remove this track.");
	}

	int limit = timeline->get_name_limit();
	int limit_end = get_size().width - timeline->get_buttons_width();
	// Left Border including space occupied by keyframes on t=0.
	int limit_start_hitbox = limit - type_icon->get_width();

	if (p_pos.x >= limit_start_hitbox && p_pos.x <= limit_end) {
		int key_idx = -1;
		float key_distance = 1e20;

		// Select should happen in the opposite order of drawing for more accurate overlap select.
		for (int i = animation->track_get_key_count(track) - 1; i >= 0; i--) {
			Rect2 rect = const_cast<AnimationTrackEdit *>(this)->get_key_rect(i, timeline->get_zoom_scale());
			float offset = animation->track_get_key_time(track, i) - timeline->get_value();
			offset = offset * timeline->get_zoom_scale() + limit;
			rect.position.x += offset;

			if (rect.has_point(p_pos)) {
				if (const_cast<AnimationTrackEdit *>(this)->is_key_selectable_by_distance()) {
					float distance = ABS(offset - p_pos.x);
					if (key_idx == -1 || distance < key_distance) {
						key_idx = i;
						key_distance = distance;
					}
				} else {
					// First one does it.
					break;
				}
			}
		}

		if (key_idx != -1) {
			String text = TTR("Time (s): ") + rtos(animation->track_get_key_time(track, key_idx)) + "\n";
			switch (animation->track_get_type(track)) {
				case Animation::TYPE_POSITION_3D: {
					Vector3 t = animation->track_get_key_value(track, key_idx);
					text += "Position: " + String(t) + "\n";
				} break;
				case Animation::TYPE_ROTATION_3D: {
					Quaternion t = animation->track_get_key_value(track, key_idx);
					text += "Rotation: " + String(t) + "\n";
				} break;
				case Animation::TYPE_SCALE_3D: {
					Vector3 t = animation->track_get_key_value(track, key_idx);
					text += "Scale: " + String(t) + "\n";
				} break;
				case Animation::TYPE_BLEND_SHAPE: {
					float t = animation->track_get_key_value(track, key_idx);
					text += "Blend Shape: " + itos(t) + "\n";
				} break;
				case Animation::TYPE_VALUE: {
					const Variant &v = animation->track_get_key_value(track, key_idx);
					text += "Type: " + Variant::get_type_name(v.get_type()) + "\n";
					Variant::Type valid_type = Variant::NIL;
					if (!_is_value_key_valid(v, valid_type)) {
						text += "Value: " + String(v) + "  (Invalid, expected type: " + Variant::get_type_name(valid_type) + ")\n";
					} else {
						text += "Value: " + String(v) + "\n";
					}
					text += "Easing: " + rtos(animation->track_get_key_transition(track, key_idx));

				} break;
				case Animation::TYPE_METHOD: {
					Dictionary d = animation->track_get_key_value(track, key_idx);
					if (d.has("method")) {
						text += String(d["method"]);
					}
					text += "(";
					Vector<Variant> args;
					if (d.has("args")) {
						args = d["args"];
					}
					for (int i = 0; i < args.size(); i++) {
						if (i > 0) {
							text += ", ";
						}
						text += String(args[i]);
					}
					text += ")\n";

				} break;
				case Animation::TYPE_BEZIER: {
					float h = animation->bezier_track_get_key_value(track, key_idx);
					text += "Value: " + rtos(h) + "\n";
					Vector2 ih = animation->bezier_track_get_key_in_handle(track, key_idx);
					text += "In-Handle: " + ih + "\n";
					Vector2 oh = animation->bezier_track_get_key_out_handle(track, key_idx);
					text += "Out-Handle: " + oh + "\n";
					int hm = animation->bezier_track_get_key_handle_mode(track, key_idx);
					text += "Handle mode: ";
					switch (hm) {
						case Animation::HANDLE_MODE_FREE: {
							text += "Free";
						} break;
						case Animation::HANDLE_MODE_BALANCED: {
							text += "Balanced";
						} break;
					}
					text += "\n";
				} break;
				case Animation::TYPE_AUDIO: {
					String stream_name = "null";
					Ref<Resource> stream = animation->audio_track_get_key_stream(track, key_idx);
					if (stream.is_valid()) {
						if (stream->get_path().is_resource_file()) {
							stream_name = stream->get_path().get_file();
						} else if (!stream->get_name().is_empty()) {
							stream_name = stream->get_name();
						} else {
							stream_name = stream->get_class();
						}
					}

					text += "Stream: " + stream_name + "\n";
					float so = animation->audio_track_get_key_start_offset(track, key_idx);
					text += "Start (s): " + rtos(so) + "\n";
					float eo = animation->audio_track_get_key_end_offset(track, key_idx);
					text += "End (s): " + rtos(eo) + "\n";
				} break;
				case Animation::TYPE_ANIMATION: {
					String name = animation->animation_track_get_key_animation(track, key_idx);
					text += "Animation Clip: " + name + "\n";
				} break;
			}
			return text;
		}
	}

	return Control::get_tooltip(p_pos);
}

void AnimationTrackEdit::gui_input(const Ref<InputEvent> &p_event) {
	ERR_FAIL_COND(p_event.is_null());

	if (p_event->is_pressed()) {
		if (ED_GET_SHORTCUT("animation_editor/duplicate_selection")->matches_event(p_event)) {
			emit_signal(SNAME("duplicate_request"));
			accept_event();
		}

		if (ED_GET_SHORTCUT("animation_editor/duplicate_selection_transposed")->matches_event(p_event)) {
			emit_signal(SNAME("duplicate_transpose_request"));
			accept_event();
		}

		if (ED_GET_SHORTCUT("animation_editor/delete_selection")->matches_event(p_event)) {
			emit_signal(SNAME("delete_request"));
			accept_event();
		}
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		Point2 pos = mb->get_position();

		if (check_rect.has_point(pos)) {
			undo_redo->create_action(TTR("Toggle Track Enabled"));
			undo_redo->add_do_method(animation.ptr(), "track_set_enabled", track, !animation->track_is_enabled(track));
			undo_redo->add_undo_method(animation.ptr(), "track_set_enabled", track, animation->track_is_enabled(track));
			undo_redo->commit_action();
			update();
			accept_event();
		}

		// Don't overlap track keys if they start at 0.
		if (path_rect.has_point(pos + Size2(type_icon->get_width(), 0))) {
			clicking_on_name = true;
			accept_event();
		}

		if (update_mode_rect.has_point(pos)) {
			if (!menu) {
				menu = memnew(PopupMenu);
				add_child(menu);
				menu->connect("id_pressed", callable_mp(this, &AnimationTrackEdit::_menu_selected));
			}
			menu->clear();
			menu->add_icon_item(get_theme_icon(SNAME("TrackContinuous"), SNAME("EditorIcons")), TTR("Continuous"), MENU_CALL_MODE_CONTINUOUS);
			menu->add_icon_item(get_theme_icon(SNAME("TrackDiscrete"), SNAME("EditorIcons")), TTR("Discrete"), MENU_CALL_MODE_DISCRETE);
			menu->add_icon_item(get_theme_icon(SNAME("TrackTrigger"), SNAME("EditorIcons")), TTR("Trigger"), MENU_CALL_MODE_TRIGGER);
			menu->add_icon_item(get_theme_icon(SNAME("TrackCapture"), SNAME("EditorIcons")), TTR("Capture"), MENU_CALL_MODE_CAPTURE);
			menu->reset_size();

			Vector2 popup_pos = get_screen_position() + update_mode_rect.position + Vector2(0, update_mode_rect.size.height);
			menu->set_position(popup_pos);
			menu->popup();
			accept_event();
		}

		if (interp_mode_rect.has_point(pos)) {
			if (!menu) {
				menu = memnew(PopupMenu);
				add_child(menu);
				menu->connect("id_pressed", callable_mp(this, &AnimationTrackEdit::_menu_selected));
			}
			menu->clear();
			menu->add_icon_item(get_theme_icon(SNAME("InterpRaw"), SNAME("EditorIcons")), TTR("Nearest"), MENU_INTERPOLATION_NEAREST);
			menu->add_icon_item(get_theme_icon(SNAME("InterpLinear"), SNAME("EditorIcons")), TTR("Linear"), MENU_INTERPOLATION_LINEAR);
			menu->add_icon_item(get_theme_icon(SNAME("InterpCubic"), SNAME("EditorIcons")), TTR("Cubic"), MENU_INTERPOLATION_CUBIC);
			menu->reset_size();

			Vector2 popup_pos = get_screen_position() + interp_mode_rect.position + Vector2(0, interp_mode_rect.size.height);
			menu->set_position(popup_pos);
			menu->popup();
			accept_event();
		}

		if (loop_wrap_rect.has_point(pos)) {
			if (!menu) {
				menu = memnew(PopupMenu);
				add_child(menu);
				menu->connect("id_pressed", callable_mp(this, &AnimationTrackEdit::_menu_selected));
			}
			menu->clear();
			menu->add_icon_item(get_theme_icon(SNAME("InterpWrapClamp"), SNAME("EditorIcons")), TTR("Clamp Loop Interp"), MENU_LOOP_CLAMP);
			menu->add_icon_item(get_theme_icon(SNAME("InterpWrapLoop"), SNAME("EditorIcons")), TTR("Wrap Loop Interp"), MENU_LOOP_WRAP);
			menu->reset_size();

			Vector2 popup_pos = get_screen_position() + loop_wrap_rect.position + Vector2(0, loop_wrap_rect.size.height);
			menu->set_position(popup_pos);
			menu->popup();
			accept_event();
		}

		if (remove_rect.has_point(pos)) {
			emit_signal(SNAME("remove_request"), track);
			accept_event();
			return;
		}

		// Check keyframes.

		if (!animation->track_is_compressed(track)) { // Selecting compressed keyframes for editing is not possible.

			float scale = timeline->get_zoom_scale();
			int limit = timeline->get_name_limit();
			int limit_end = get_size().width - timeline->get_buttons_width();
			// Left Border including space occupied by keyframes on t=0.
			int limit_start_hitbox = limit - type_icon->get_width();

			if (pos.x >= limit_start_hitbox && pos.x <= limit_end) {
				int key_idx = -1;
				float key_distance = 1e20;

				// Select should happen in the opposite order of drawing for more accurate overlap select.
				for (int i = animation->track_get_key_count(track) - 1; i >= 0; i--) {
					Rect2 rect = get_key_rect(i, scale);
					float offset = animation->track_get_key_time(track, i) - timeline->get_value();
					offset = offset * scale + limit;
					rect.position.x += offset;

					if (rect.has_point(pos)) {
						if (is_key_selectable_by_distance()) {
							float distance = ABS(offset - pos.x);
							if (key_idx == -1 || distance < key_distance) {
								key_idx = i;
								key_distance = distance;
							}
						} else {
							// First one does it.
							key_idx = i;
							break;
						}
					}
				}

				if (key_idx != -1) {
					if (mb->is_command_pressed() || mb->is_shift_pressed()) {
						if (editor->is_key_selected(track, key_idx)) {
							emit_signal(SNAME("deselect_key"), key_idx);
						} else {
							emit_signal(SNAME("select_key"), key_idx, false);
							moving_selection_attempt = true;
							select_single_attempt = -1;
							moving_selection_from_ofs = (mb->get_position().x - limit) / timeline->get_zoom_scale();
						}
					} else {
						if (!editor->is_key_selected(track, key_idx)) {
							emit_signal(SNAME("select_key"), key_idx, true);
							select_single_attempt = -1;
						} else {
							select_single_attempt = key_idx;
						}

						moving_selection_attempt = true;
						moving_selection_from_ofs = (mb->get_position().x - limit) / timeline->get_zoom_scale();
					}
					accept_event();
				}
			}
		}
	}

	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::RIGHT) {
		Point2 pos = mb->get_position();
		if (pos.x >= timeline->get_name_limit() && pos.x <= get_size().width - timeline->get_buttons_width()) {
			// Can do something with menu too! show insert key.
			float offset = (pos.x - timeline->get_name_limit()) / timeline->get_zoom_scale();
			if (!menu) {
				menu = memnew(PopupMenu);
				add_child(menu);
				menu->connect("id_pressed", callable_mp(this, &AnimationTrackEdit::_menu_selected));
			}

			menu->clear();
			menu->add_icon_item(get_theme_icon(SNAME("Key"), SNAME("EditorIcons")), TTR("Insert Key"), MENU_KEY_INSERT);
			if (editor->is_selection_active()) {
				menu->add_separator();
				menu->add_icon_item(get_theme_icon(SNAME("Duplicate"), SNAME("EditorIcons")), TTR("Duplicate Key(s)"), MENU_KEY_DUPLICATE);

				AnimationPlayer *player = AnimationPlayerEditor::get_singleton()->get_player();
				if (!player->has_animation(SceneStringNames::get_singleton()->RESET) || animation != player->get_animation(SceneStringNames::get_singleton()->RESET)) {
					menu->add_icon_item(get_theme_icon(SNAME("Reload"), SNAME("EditorIcons")), TTR("Add RESET Value(s)"), MENU_KEY_ADD_RESET);
				}

				menu->add_separator();
				menu->add_icon_item(get_theme_icon(SNAME("Remove"), SNAME("EditorIcons")), TTR("Delete Key(s)"), MENU_KEY_DELETE);
			}
			menu->reset_size();

			menu->set_position(get_screen_position() + get_local_mouse_position());
			menu->popup();

			insert_at_pos = offset + timeline->get_value();
			accept_event();
		}
	}

	if (mb.is_valid() && !mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT && clicking_on_name) {
		if (!path) {
			path_popup = memnew(Popup);
			path_popup->set_wrap_controls(true);
			add_child(path_popup);
			path = memnew(LineEdit);
			path_popup->add_child(path);
			path->set_anchors_and_offsets_preset(PRESET_WIDE);
			path->connect("text_submitted", callable_mp(this, &AnimationTrackEdit::_path_submitted));
		}

		path->set_text(animation->track_get_path(track));
		Vector2 theme_ofs = path->get_theme_stylebox(SNAME("normal"), SNAME("LineEdit"))->get_offset();
		path_popup->set_position(get_screen_position() + path_rect.position - theme_ofs);
		path_popup->set_size(path_rect.size);
		path_popup->popup();
		path->grab_focus();
		path->set_caret_column(path->get_text().length());
		clicking_on_name = false;
	}

	if (mb.is_valid() && moving_selection_attempt) {
		if (!mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
			moving_selection_attempt = false;
			if (moving_selection) {
				emit_signal(SNAME("move_selection_commit"));
			} else if (select_single_attempt != -1) {
				emit_signal(SNAME("select_key"), select_single_attempt, true);
			}
			moving_selection = false;
			select_single_attempt = -1;
		}

		if (moving_selection && mb->is_pressed() && mb->get_button_index() == MouseButton::RIGHT) {
			moving_selection_attempt = false;
			moving_selection = false;
			emit_signal(SNAME("move_selection_cancel"));
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		const int previous_hovering_key_idx = hovering_key_idx;

		// Hovering compressed keyframes for editing is not possible.
		if (!animation->track_is_compressed(track)) {
			const float scale = timeline->get_zoom_scale();
			const int limit = timeline->get_name_limit();
			const int limit_end = get_size().width - timeline->get_buttons_width();
			// Left Border including space occupied by keyframes on t=0.
			const int limit_start_hitbox = limit - type_icon->get_width();
			const Point2 pos = mm->get_position();

			if (pos.x >= limit_start_hitbox && pos.x <= limit_end) {
				// Use the same logic as key selection to ensure that hovering accurately represents
				// which key will be selected when clicking.
				int key_idx = -1;
				float key_distance = 1e20;

				hovering_key_idx = -1;

				// Hovering should happen in the opposite order of drawing for more accurate overlap hovering.
				for (int i = animation->track_get_key_count(track) - 1; i >= 0; i--) {
					Rect2 rect = get_key_rect(i, scale);
					float offset = animation->track_get_key_time(track, i) - timeline->get_value();
					offset = offset * scale + limit;
					rect.position.x += offset;

					if (rect.has_point(pos)) {
						if (is_key_selectable_by_distance()) {
							const float distance = ABS(offset - pos.x);
							if (key_idx == -1 || distance < key_distance) {
								key_idx = i;
								key_distance = distance;
								hovering_key_idx = i;
							}
						} else {
							// First one does it.
							hovering_key_idx = i;
							break;
						}
					}
				}

				if (hovering_key_idx != previous_hovering_key_idx) {
					// Required to draw keyframe hover feedback on the correct keyframe.
					update();
				}
			}
		}
	}

	if (mm.is_valid() && (mm->get_button_mask() & MouseButton::MASK_LEFT) != MouseButton::NONE && moving_selection_attempt) {
		if (!moving_selection) {
			moving_selection = true;
			emit_signal(SNAME("move_selection_begin"));
		}

		float new_ofs = (mm->get_position().x - timeline->get_name_limit()) / timeline->get_zoom_scale();
		emit_signal(SNAME("move_selection"), new_ofs - moving_selection_from_ofs);
	}
}

Variant AnimationTrackEdit::get_drag_data(const Point2 &p_point) {
	if (!clicking_on_name) {
		return Variant();
	}

	Dictionary drag_data;
	drag_data["type"] = "animation_track";
	String base_path = animation->track_get_path(track);
	base_path = base_path.get_slice(":", 0); // Remove sub-path.
	drag_data["group"] = base_path;
	drag_data["index"] = track;

	Button *tb = memnew(Button);
	tb->set_flat(true);
	tb->set_text(path_cache);
	tb->set_icon(icon_cache);
	set_drag_preview(tb);

	clicking_on_name = false;

	return drag_data;
}

bool AnimationTrackEdit::can_drop_data(const Point2 &p_point, const Variant &p_data) const {
	Dictionary d = p_data;
	if (!d.has("type")) {
		return false;
	}

	String type = d["type"];
	if (type != "animation_track") {
		return false;
	}

	// Don't allow moving tracks outside their groups.
	if (get_editor()->is_grouping_tracks()) {
		String base_path = animation->track_get_path(track);
		base_path = base_path.get_slice(":", 0); // Remove sub-path.
		if (d["group"] != base_path) {
			return false;
		}
	}

	if (p_point.y < get_size().height / 2) {
		dropping_at = -1;
	} else {
		dropping_at = 1;
	}

	const_cast<AnimationTrackEdit *>(this)->update();
	const_cast<AnimationTrackEdit *>(this)->emit_signal(SNAME("drop_attempted"), track);

	return true;
}

void AnimationTrackEdit::drop_data(const Point2 &p_point, const Variant &p_data) {
	Dictionary d = p_data;
	if (!d.has("type")) {
		return;
	}

	String type = d["type"];
	if (type != "animation_track") {
		return;
	}

	// Don't allow moving tracks outside their groups.
	if (get_editor()->is_grouping_tracks()) {
		String base_path = animation->track_get_path(track);
		base_path = base_path.get_slice(":", 0); // Remove sub-path.
		if (d["group"] != base_path) {
			return;
		}
	}

	int from_track = d["index"];

	if (dropping_at < 0) {
		emit_signal(SNAME("dropped"), from_track, track);
	} else {
		emit_signal(SNAME("dropped"), from_track, track + 1);
	}
}

void AnimationTrackEdit::_menu_selected(int p_index) {
	switch (p_index) {
		case MENU_CALL_MODE_CONTINUOUS:
		case MENU_CALL_MODE_DISCRETE:
		case MENU_CALL_MODE_TRIGGER:
		case MENU_CALL_MODE_CAPTURE: {
			Animation::UpdateMode update_mode = Animation::UpdateMode(p_index);
			undo_redo->create_action(TTR("Change Animation Update Mode"));
			undo_redo->add_do_method(animation.ptr(), "value_track_set_update_mode", track, update_mode);
			undo_redo->add_undo_method(animation.ptr(), "value_track_set_update_mode", track, animation->value_track_get_update_mode(track));
			undo_redo->commit_action();
			update();

		} break;
		case MENU_INTERPOLATION_NEAREST:
		case MENU_INTERPOLATION_LINEAR:
		case MENU_INTERPOLATION_CUBIC: {
			Animation::InterpolationType interp_mode = Animation::InterpolationType(p_index - MENU_INTERPOLATION_NEAREST);
			undo_redo->create_action(TTR("Change Animation Interpolation Mode"));
			undo_redo->add_do_method(animation.ptr(), "track_set_interpolation_type", track, interp_mode);
			undo_redo->add_undo_method(animation.ptr(), "track_set_interpolation_type", track, animation->track_get_interpolation_type(track));
			undo_redo->commit_action();
			update();
		} break;
		case MENU_LOOP_WRAP:
		case MENU_LOOP_CLAMP: {
			bool loop_wrap = p_index == MENU_LOOP_WRAP;
			undo_redo->create_action(TTR("Change Animation Loop Mode"));
			undo_redo->add_do_method(animation.ptr(), "track_set_interpolation_loop_wrap", track, loop_wrap);
			undo_redo->add_undo_method(animation.ptr(), "track_set_interpolation_loop_wrap", track, animation->track_get_interpolation_loop_wrap(track));
			undo_redo->commit_action();
			update();

		} break;
		case MENU_KEY_INSERT: {
			emit_signal(SNAME("insert_key"), insert_at_pos);
		} break;
		case MENU_KEY_DUPLICATE: {
			emit_signal(SNAME("duplicate_request"));
		} break;
		case MENU_KEY_ADD_RESET: {
			emit_signal(SNAME("create_reset_request"));

		} break;
		case MENU_KEY_DELETE: {
			emit_signal(SNAME("delete_request"));

		} break;
	}
}

void AnimationTrackEdit::cancel_drop() {
	if (dropping_at != 0) {
		dropping_at = 0;
		update();
	}
}

void AnimationTrackEdit::set_in_group(bool p_enable) {
	in_group = p_enable;
	update();
}

void AnimationTrackEdit::append_to_selection(const Rect2 &p_box, bool p_deselection) {
	if (animation->track_is_compressed(track)) {
		return; // Compressed keyframes can't be edited
	}
	// Left Border including space occupied by keyframes on t=0.
	int limit_start_hitbox = timeline->get_name_limit() - type_icon->get_width();
	Rect2 select_rect(limit_start_hitbox, 0, get_size().width - timeline->get_name_limit() - timeline->get_buttons_width(), get_size().height);
	select_rect = select_rect.intersection(p_box);

	// Select should happen in the opposite order of drawing for more accurate overlap select.
	for (int i = animation->track_get_key_count(track) - 1; i >= 0; i--) {
		Rect2 rect = const_cast<AnimationTrackEdit *>(this)->get_key_rect(i, timeline->get_zoom_scale());
		float offset = animation->track_get_key_time(track, i) - timeline->get_value();
		offset = offset * timeline->get_zoom_scale() + timeline->get_name_limit();
		rect.position.x += offset;

		if (select_rect.intersects(rect)) {
			if (p_deselection) {
				emit_signal(SNAME("deselect_key"), i);
			} else {
				emit_signal(SNAME("select_key"), i, false);
			}
		}
	}
}

void AnimationTrackEdit::_bind_methods() {
	ADD_SIGNAL(MethodInfo("timeline_changed", PropertyInfo(Variant::FLOAT, "position"), PropertyInfo(Variant::BOOL, "drag"), PropertyInfo(Variant::BOOL, "timeline_only")));
	ADD_SIGNAL(MethodInfo("remove_request", PropertyInfo(Variant::INT, "track")));
	ADD_SIGNAL(MethodInfo("dropped", PropertyInfo(Variant::INT, "from_track"), PropertyInfo(Variant::INT, "to_track")));
	ADD_SIGNAL(MethodInfo("insert_key", PropertyInfo(Variant::FLOAT, "offset")));
	ADD_SIGNAL(MethodInfo("select_key", PropertyInfo(Variant::INT, "index"), PropertyInfo(Variant::BOOL, "single")));
	ADD_SIGNAL(MethodInfo("deselect_key", PropertyInfo(Variant::INT, "index")));
	ADD_SIGNAL(MethodInfo("bezier_edit"));

	ADD_SIGNAL(MethodInfo("move_selection_begin"));
	ADD_SIGNAL(MethodInfo("move_selection", PropertyInfo(Variant::FLOAT, "offset")));
	ADD_SIGNAL(MethodInfo("move_selection_commit"));
	ADD_SIGNAL(MethodInfo("move_selection_cancel"));

	ADD_SIGNAL(MethodInfo("duplicate_request"));
	ADD_SIGNAL(MethodInfo("create_reset_request"));
	ADD_SIGNAL(MethodInfo("duplicate_transpose_request"));
	ADD_SIGNAL(MethodInfo("delete_request"));
}

AnimationTrackEdit::AnimationTrackEdit() {
	play_position = memnew(Control);
	play_position->set_mouse_filter(MOUSE_FILTER_PASS);
	add_child(play_position);
	play_position->set_anchors_and_offsets_preset(PRESET_WIDE);
	play_position->connect("draw", callable_mp(this, &AnimationTrackEdit::_play_position_draw));
	set_focus_mode(FOCUS_CLICK);
	set_mouse_filter(MOUSE_FILTER_PASS); // Scroll has to work too for selection.
}

//////////////////////////////////////

AnimationTrackEdit *AnimationTrackEditPlugin::create_value_track_edit(Object *p_object, Variant::Type p_type, const String &p_property, PropertyHint p_hint, const String &p_hint_string, int p_usage) {
	if (get_script_instance()) {
		Variant args[6] = {
			p_object,
			p_type,
			p_property,
			p_hint,
			p_hint_string,
			p_usage
		};

		Variant *argptrs[6] = {
			&args[0],
			&args[1],
			&args[2],
			&args[3],
			&args[4],
			&args[5]
		};

		Callable::CallError ce;
		return Object::cast_to<AnimationTrackEdit>(get_script_instance()->callp("create_value_track_edit", (const Variant **)&argptrs, 6, ce).operator Object *());
	}
	return nullptr;
}

AnimationTrackEdit *AnimationTrackEditPlugin::create_audio_track_edit() {
	if (get_script_instance()) {
		return Object::cast_to<AnimationTrackEdit>(get_script_instance()->call("create_audio_track_edit").operator Object *());
	}
	return nullptr;
}

AnimationTrackEdit *AnimationTrackEditPlugin::create_animation_track_edit(Object *p_object) {
	if (get_script_instance()) {
		return Object::cast_to<AnimationTrackEdit>(get_script_instance()->call("create_animation_track_edit", p_object).operator Object *());
	}
	return nullptr;
}

///////////////////////////////////////

void AnimationTrackEditGroup::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAW: {
			Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
			int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
			int separation = get_theme_constant(SNAME("h_separation"), SNAME("ItemList"));
			Color color = get_theme_color(SNAME("font_color"), SNAME("Label"));

			if (root && root->has_node(node)) {
				Node *n = root->get_node(node);
				if (n && EditorNode::get_singleton()->get_editor_selection()->is_selected(n)) {
					color = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
				}
			}

			Color bgcol = get_theme_color(SNAME("dark_color_2"), SNAME("Editor"));
			bgcol.a *= 0.6;
			draw_rect(Rect2(Point2(), get_size()), bgcol);
			Color linecolor = color;
			linecolor.a = 0.2;

			draw_line(Point2(), Point2(get_size().width, 0), linecolor, Math::round(EDSCALE));
			draw_line(Point2(timeline->get_name_limit(), 0), Point2(timeline->get_name_limit(), get_size().height), linecolor, Math::round(EDSCALE));
			draw_line(Point2(get_size().width - timeline->get_buttons_width(), 0), Point2(get_size().width - timeline->get_buttons_width(), get_size().height), linecolor, Math::round(EDSCALE));

			int ofs = 0;
			draw_texture(icon, Point2(ofs, int(get_size().height - icon->get_height()) / 2));
			ofs += separation + icon->get_width();
			draw_string(font, Point2(ofs, int(get_size().height - font->get_height(font_size)) / 2 + font->get_ascent(font_size)), node_name, HORIZONTAL_ALIGNMENT_LEFT, timeline->get_name_limit() - ofs, font_size, color);

			int px = (-timeline->get_value() + timeline->get_play_position()) * timeline->get_zoom_scale() + timeline->get_name_limit();

			if (px >= timeline->get_name_limit() && px < (get_size().width - timeline->get_buttons_width())) {
				Color accent = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
				draw_line(Point2(px, 0), Point2(px, get_size().height), accent, Math::round(2 * EDSCALE));
			}
		} break;
	}
}

void AnimationTrackEditGroup::set_type_and_name(const Ref<Texture2D> &p_type, const String &p_name, const NodePath &p_node) {
	icon = p_type;
	node_name = p_name;
	node = p_node;
	update();
	update_minimum_size();
}

Size2 AnimationTrackEditGroup::get_minimum_size() const {
	Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
	int font_size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
	int separation = get_theme_constant(SNAME("v_separation"), SNAME("ItemList"));

	return Vector2(0, MAX(font->get_height(font_size), icon->get_height()) + separation);
}

void AnimationTrackEditGroup::set_timeline(AnimationTimelineEdit *p_timeline) {
	timeline = p_timeline;
	timeline->connect("zoom_changed", callable_mp(this, &AnimationTrackEditGroup::_zoom_changed));
	timeline->connect("name_limit_changed", callable_mp(this, &AnimationTrackEditGroup::_zoom_changed));
}

void AnimationTrackEditGroup::set_root(Node *p_root) {
	root = p_root;
	update();
}

void AnimationTrackEditGroup::_zoom_changed() {
	update();
}

void AnimationTrackEditGroup::_bind_methods() {
}

AnimationTrackEditGroup::AnimationTrackEditGroup() {
	set_mouse_filter(MOUSE_FILTER_PASS);
}

//////////////////////////////////////

void AnimationTrackEditor::add_track_edit_plugin(const Ref<AnimationTrackEditPlugin> &p_plugin) {
	if (track_edit_plugins.has(p_plugin)) {
		return;
	}
	track_edit_plugins.push_back(p_plugin);
}

void AnimationTrackEditor::remove_track_edit_plugin(const Ref<AnimationTrackEditPlugin> &p_plugin) {
	track_edit_plugins.erase(p_plugin);
}

void AnimationTrackEditor::set_animation(const Ref<Animation> &p_anim) {
	if (animation != p_anim && _get_track_selected() >= 0) {
		track_edits[_get_track_selected()]->release_focus();
	}
	if (animation.is_valid()) {
		animation->disconnect("changed", callable_mp(this, &AnimationTrackEditor::_animation_changed));
		_clear_selection();
	}
	animation = p_anim;
	timeline->set_animation(p_anim);

	_cancel_bezier_edit();
	_update_tracks();

	if (animation.is_valid()) {
		animation->connect("changed", callable_mp(this, &AnimationTrackEditor::_animation_changed));

		hscroll->show();
		edit->set_disabled(false);
		step->set_block_signals(true);

		_update_step_spinbox();
		step->set_block_signals(false);
		step->set_read_only(false);
		snap->set_disabled(false);
		snap_mode->set_disabled(false);

		bezier_edit_icon->set_disabled(true);

		imported_anim_warning->hide();
		bool import_warning_done = false;
		bool bezier_done = false;
		for (int i = 0; i < animation->get_track_count(); i++) {
			if (animation->track_is_imported(i)) {
				imported_anim_warning->show();
				import_warning_done = true;
			}
			if (animation->track_get_type(i) == Animation::TrackType::TYPE_BEZIER) {
				bezier_edit_icon->set_disabled(false);
				bezier_done = true;
			}
			if (import_warning_done && bezier_done) {
				break;
			}
		}

	} else {
		hscroll->hide();
		edit->set_disabled(true);
		step->set_block_signals(true);
		step->set_value(0);
		step->set_block_signals(false);
		step->set_read_only(true);
		snap->set_disabled(true);
		snap_mode->set_disabled(true);
		bezier_edit_icon->set_disabled(true);
	}
}

Ref<Animation> AnimationTrackEditor::get_current_animation() const {
	return animation;
}

void AnimationTrackEditor::_root_removed() {
	root = nullptr;
}

void AnimationTrackEditor::set_root(Node *p_root) {
	if (root) {
		root->disconnect("tree_exiting", callable_mp(this, &AnimationTrackEditor::_root_removed));
	}

	root = p_root;

	if (root) {
		root->connect("tree_exiting", callable_mp(this, &AnimationTrackEditor::_root_removed), make_binds(), CONNECT_ONESHOT);
	}

	_update_tracks();
}

Node *AnimationTrackEditor::get_root() const {
	return root;
}

void AnimationTrackEditor::update_keying() {
	bool keying_enabled = false;

	EditorSelectionHistory *editor_history = EditorNode::get_singleton()->get_editor_selection_history();
	if (is_visible_in_tree() && animation.is_valid() && editor_history->get_path_size() > 0) {
		Object *obj = ObjectDB::get_instance(editor_history->get_path_object(0));
		keying_enabled = Object::cast_to<Node>(obj) != nullptr;
	}

	if (keying_enabled == keying) {
		return;
	}

	keying = keying_enabled;

	emit_signal(SNAME("keying_changed"));
}

bool AnimationTrackEditor::has_keying() const {
	return keying;
}

Dictionary AnimationTrackEditor::get_state() const {
	Dictionary state;
	state["fps_mode"] = timeline->is_using_fps();
	state["zoom"] = zoom->get_value();
	state["offset"] = timeline->get_value();
	state["v_scroll"] = scroll->get_v_scroll_bar()->get_value();
	return state;
}

void AnimationTrackEditor::set_state(const Dictionary &p_state) {
	if (p_state.has("fps_mode")) {
		bool fps_mode = p_state["fps_mode"];
		if (fps_mode) {
			snap_mode->select(1);
		} else {
			snap_mode->select(0);
		}
		_snap_mode_changed(snap_mode->get_selected());
	} else {
		snap_mode->select(0);
		_snap_mode_changed(snap_mode->get_selected());
	}
	if (p_state.has("zoom")) {
		zoom->set_value(p_state["zoom"]);
	} else {
		zoom->set_value(1.0);
	}
	if (p_state.has("offset")) {
		timeline->set_value(p_state["offset"]);
	} else {
		timeline->set_value(0);
	}
	if (p_state.has("v_scroll")) {
		scroll->get_v_scroll_bar()->set_value(p_state["v_scroll"]);
	} else {
		scroll->get_v_scroll_bar()->set_value(0);
	}
}

void AnimationTrackEditor::cleanup() {
	set_animation(Ref<Animation>());
}

void AnimationTrackEditor::_name_limit_changed() {
	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->update();
	}
}

void AnimationTrackEditor::_timeline_changed(float p_new_pos, bool p_drag, bool p_timeline_only) {
	emit_signal(SNAME("timeline_changed"), p_new_pos, p_drag, p_timeline_only);
}

void AnimationTrackEditor::_track_remove_request(int p_track) {
	_animation_track_remove_request(p_track, animation);
}

void AnimationTrackEditor::_animation_track_remove_request(int p_track, Ref<Animation> p_from_animation) {
	if (p_from_animation->track_is_compressed(p_track)) {
		EditorNode::get_singleton()->show_warning(TTR("Compressed tracks can't be edited or removed. Re-import the animation with compression disabled in order to edit."));
		return;
	}
	int idx = p_track;
	if (idx >= 0 && idx < p_from_animation->get_track_count()) {
		undo_redo->create_action(TTR("Remove Anim Track"));

		// Remove corresponding reset tracks if they are no longer needed.
		AnimationPlayer *player = AnimationPlayerEditor::get_singleton()->get_player();
		if (player->has_animation(SceneStringNames::get_singleton()->RESET)) {
			Ref<Animation> reset = player->get_animation(SceneStringNames::get_singleton()->RESET);
			if (reset != p_from_animation) {
				for (int i = 0; i < reset->get_track_count(); i++) {
					if (reset->track_get_path(i) == p_from_animation->track_get_path(p_track)) {
						// Check if the reset track isn't used by other animations.
						bool used = false;
						List<StringName> animation_list;
						player->get_animation_list(&animation_list);

						for (const StringName &anim_name : animation_list) {
							Ref<Animation> anim = player->get_animation(anim_name);
							if (anim == p_from_animation || anim == reset) {
								continue;
							}

							for (int j = 0; j < anim->get_track_count(); j++) {
								if (anim->track_get_path(j) == reset->track_get_path(i)) {
									used = true;
									break;
								}
							}

							if (used) {
								break;
							}
						}

						if (!used) {
							_animation_track_remove_request(i, reset);
						}
						break;
					}
				}
			}
		}

		undo_redo->add_do_method(this, "_clear_selection", false);
		undo_redo->add_do_method(p_from_animation.ptr(), "remove_track", idx);
		undo_redo->add_undo_method(p_from_animation.ptr(), "add_track", p_from_animation->track_get_type(idx), idx);
		undo_redo->add_undo_method(p_from_animation.ptr(), "track_set_path", idx, p_from_animation->track_get_path(idx));

		// TODO interpolation.
		for (int i = 0; i < p_from_animation->track_get_key_count(idx); i++) {
			Variant v = p_from_animation->track_get_key_value(idx, i);
			float time = p_from_animation->track_get_key_time(idx, i);
			float trans = p_from_animation->track_get_key_transition(idx, i);

			undo_redo->add_undo_method(p_from_animation.ptr(), "track_insert_key", idx, time, v);
			undo_redo->add_undo_method(p_from_animation.ptr(), "track_set_key_transition", idx, i, trans);
		}

		undo_redo->add_undo_method(p_from_animation.ptr(), "track_set_interpolation_type", idx, p_from_animation->track_get_interpolation_type(idx));
		if (p_from_animation->track_get_type(idx) == Animation::TYPE_VALUE) {
			undo_redo->add_undo_method(p_from_animation.ptr(), "value_track_set_update_mode", idx, p_from_animation->value_track_get_update_mode(idx));
		}

		undo_redo->commit_action();
	}
}

void AnimationTrackEditor::_track_grab_focus(int p_track) {
	// Don't steal focus if not working with the track editor.
	if (Object::cast_to<AnimationTrackEdit>(get_viewport()->gui_get_focus_owner())) {
		track_edits[p_track]->grab_focus();
	}
}

void AnimationTrackEditor::set_anim_pos(float p_pos) {
	timeline->set_play_position(p_pos);
	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->set_play_position(p_pos);
	}
	for (int i = 0; i < groups.size(); i++) {
		groups[i]->update();
	}
	bezier_edit->set_play_position(p_pos);
}

static bool track_type_is_resettable(Animation::TrackType p_type) {
	switch (p_type) {
		case Animation::TYPE_VALUE:
			[[fallthrough]];
		case Animation::TYPE_BLEND_SHAPE:
			[[fallthrough]];
		case Animation::TYPE_BEZIER:
			[[fallthrough]];
		case Animation::TYPE_POSITION_3D:
			[[fallthrough]];
		case Animation::TYPE_ROTATION_3D:
			[[fallthrough]];
		case Animation::TYPE_SCALE_3D:
			return true;
		default:
			return false;
	}
}

void AnimationTrackEditor::make_insert_queue() {
	insert_data.clear();
	insert_queue = true;
}

void AnimationTrackEditor::commit_insert_queue() {
	bool reset_allowed = true;
	AnimationPlayer *player = AnimationPlayerEditor::get_singleton()->get_player();
	if (player->has_animation(SceneStringNames::get_singleton()->RESET) && player->get_animation(SceneStringNames::get_singleton()->RESET) == animation) {
		// Avoid messing with the reset animation itself.
		reset_allowed = false;
	} else {
		bool some_resettable = false;
		for (int i = 0; i < insert_data.size(); i++) {
			if (track_type_is_resettable(insert_data[i].type)) {
				some_resettable = true;
				break;
			}
		}
		if (!some_resettable) {
			reset_allowed = false;
		}
	}

	// Organize insert data.
	int num_tracks = 0;
	String last_track_query;
	bool all_bezier = true;
	for (int i = 0; i < insert_data.size(); i++) {
		if (insert_data[i].type != Animation::TYPE_VALUE && insert_data[i].type != Animation::TYPE_BEZIER) {
			all_bezier = false;
		}

		if (insert_data[i].track_idx == -1) {
			++num_tracks;
			last_track_query = insert_data[i].query;
		}

		if (insert_data[i].type != Animation::TYPE_VALUE) {
			continue;
		}

		switch (insert_data[i].value.get_type()) {
			case Variant::INT:
			case Variant::FLOAT:
			case Variant::VECTOR2:
			case Variant::VECTOR3:
			case Variant::QUATERNION:
			case Variant::PLANE:
			case Variant::COLOR: {
				// Valid.
			} break;
			default: {
				all_bezier = false;
			}
		}
	}

	if (bool(EDITOR_GET("editors/animation/confirm_insert_track")) && num_tracks > 0) {
		// Potentially a new key, does not exist.
		if (num_tracks == 1) {
			// TRANSLATORS: %s will be replaced by a phrase describing the target of track.
			insert_confirm_text->set_text(vformat(TTR("Create new track for %s and insert key?"), last_track_query));
		} else {
			insert_confirm_text->set_text(vformat(TTR("Create %d new tracks and insert keys?"), num_tracks));
		}

		insert_confirm_bezier->set_visible(all_bezier);
		insert_confirm_reset->set_visible(reset_allowed);

		insert_confirm->get_ok_button()->set_text(TTR("Create"));
		insert_confirm->popup_centered();
	} else {
		_insert_track(reset_allowed && EDITOR_GET("editors/animation/default_create_reset_tracks"), all_bezier && EDITOR_GET("editors/animation/default_create_bezier_tracks"));
	}

	insert_queue = false;
}

void AnimationTrackEditor::_query_insert(const InsertData &p_id) {
	if (!insert_queue) {
		insert_data.clear();
	}

	for (const InsertData &E : insert_data) {
		// Prevent insertion of multiple tracks.
		if (E.path == p_id.path && E.type == p_id.type) {
			return; // Already inserted a track this frame.
		}
	}

	insert_data.push_back(p_id);

	// Without queue, commit immediately.
	if (!insert_queue) {
		commit_insert_queue();
	}
}

void AnimationTrackEditor::_insert_track(bool p_create_reset, bool p_create_beziers) {
	undo_redo->create_action(TTR("Anim Insert"));

	Ref<Animation> reset_anim;
	if (p_create_reset) {
		reset_anim = _create_and_get_reset_animation();
	}

	TrackIndices next_tracks(animation.ptr(), reset_anim.ptr());
	bool advance = false;
	while (insert_data.size()) {
		if (insert_data.front()->get().advance) {
			advance = true;
		}
		next_tracks = _confirm_insert(insert_data.front()->get(), next_tracks, p_create_reset, reset_anim, p_create_beziers);
		insert_data.pop_front();
	}

	undo_redo->commit_action();

	if (advance) {
		float step = animation->get_step();
		if (step == 0) {
			step = 1;
		}

		float pos = timeline->get_play_position();

		pos = Math::snapped(pos + step, step);
		if (pos > animation->get_length()) {
			pos = animation->get_length();
		}
		set_anim_pos(pos);
		emit_signal(SNAME("timeline_changed"), pos, true, false);
	}
}

void AnimationTrackEditor::insert_transform_key(Node3D *p_node, const String &p_sub, const Animation::TrackType p_type, const Variant p_value) {
	ERR_FAIL_COND(!root);
	ERR_FAIL_COND_MSG(
			(p_type != Animation::TYPE_POSITION_3D && p_type != Animation::TYPE_ROTATION_3D && p_type != Animation::TYPE_SCALE_3D),
			"Track type must be Position/Rotation/Scale 3D.");
	if (!keying) {
		return;
	}
	if (!animation.is_valid()) {
		return;
	}

	// Let's build a node path.
	String path = root->get_path_to(p_node);
	if (!p_sub.is_empty()) {
		path += ":" + p_sub;
	}

	NodePath np = path;

	int track_idx = -1;

	for (int i = 0; i < animation->get_track_count(); i++) {
		if (animation->track_get_path(i) != np) {
			continue;
		}
		if (animation->track_get_type(i) != p_type) {
			continue;
		}
		track_idx = i;
	}

	InsertData id;
	id.path = np;
	// TRANSLATORS: This describes the target of new animation track, will be inserted into another string.
	id.query = vformat(TTR("node '%s'"), p_node->get_name());
	id.advance = false;
	id.track_idx = track_idx;
	id.value = p_value;
	id.type = p_type;
	_query_insert(id);
}

bool AnimationTrackEditor::has_track(Node3D *p_node, const String &p_sub, const Animation::TrackType p_type) {
	ERR_FAIL_COND_V(!root, false);
	if (!keying) {
		return false;
	}
	if (!animation.is_valid()) {
		return false;
	}

	// Let's build a node path.
	String path = root->get_path_to(p_node);
	if (!p_sub.is_empty()) {
		path += ":" + p_sub;
	}

	int track_id = animation->find_track(path, p_type);
	if (track_id >= 0) {
		return true;
	}
	return false;
}

void AnimationTrackEditor::_insert_animation_key(NodePath p_path, const Variant &p_value) {
	String path = p_path;

	// Animation property is a special case, always creates an animation track.
	for (int i = 0; i < animation->get_track_count(); i++) {
		String np = animation->track_get_path(i);

		if (path == np && animation->track_get_type(i) == Animation::TYPE_ANIMATION) {
			// Exists.
			InsertData id;
			id.path = path;
			id.track_idx = i;
			id.value = p_value;
			id.type = Animation::TYPE_ANIMATION;
			// TRANSLATORS: This describes the target of new animation track, will be inserted into another string.
			id.query = TTR("animation");
			id.advance = false;
			// Dialog insert.
			_query_insert(id);
			return;
		}
	}

	InsertData id;
	id.path = path;
	id.track_idx = -1;
	id.value = p_value;
	id.type = Animation::TYPE_ANIMATION;
	id.query = TTR("animation");
	id.advance = false;
	// Dialog insert.
	_query_insert(id);
}

void AnimationTrackEditor::insert_node_value_key(Node *p_node, const String &p_property, const Variant &p_value, bool p_only_if_exists) {
	ERR_FAIL_COND(!root);
	// Let's build a node path.

	Node *node = p_node;

	String path = root->get_path_to(node);

	if (Object::cast_to<AnimationPlayer>(node) && p_property == "current_animation") {
		if (node == AnimationPlayerEditor::get_singleton()->get_player()) {
			EditorNode::get_singleton()->show_warning(TTR("AnimationPlayer can't animate itself, only other players."));
			return;
		}
		_insert_animation_key(path, p_value);
		return;
	}

	EditorSelectionHistory *history = EditorNode::get_singleton()->get_editor_selection_history();
	for (int i = 1; i < history->get_path_size(); i++) {
		String prop = history->get_path_property(i);
		ERR_FAIL_COND(prop.is_empty());
		path += ":" + prop;
	}

	path += ":" + p_property;

	NodePath np = path;

	// Locate track.

	bool inserted = false;

	for (int i = 0; i < animation->get_track_count(); i++) {
		if (animation->track_get_type(i) == Animation::TYPE_VALUE) {
			if (animation->track_get_path(i) != np) {
				continue;
			}

			InsertData id;
			id.path = np;
			id.track_idx = i;
			id.value = p_value;
			id.type = Animation::TYPE_VALUE;
			// TRANSLATORS: This describes the target of new animation track, will be inserted into another string.
			id.query = vformat(TTR("property '%s'"), p_property);
			id.advance = false;
			// Dialog insert.
			_query_insert(id);
			inserted = true;
		} else if (animation->track_get_type(i) == Animation::TYPE_BEZIER) {
			Variant value;
			String track_path = animation->track_get_path(i);
			if (track_path == np) {
				value = p_value; // All good.
			} else {
				int sep = track_path.rfind(":");
				if (sep != -1) {
					String base_path = track_path.substr(0, sep);
					if (base_path == np) {
						String value_name = track_path.substr(sep + 1);
						value = p_value.get(value_name);
					} else {
						continue;
					}
				} else {
					continue;
				}
			}

			InsertData id;
			id.path = animation->track_get_path(i);
			id.track_idx = i;
			id.value = value;
			id.type = Animation::TYPE_BEZIER;
			id.query = vformat(TTR("property '%s'"), p_property);
			id.advance = false;
			// Dialog insert.
			_query_insert(id);
			inserted = true;
		}
	}

	if (inserted || p_only_if_exists) {
		return;
	}
	InsertData id;
	id.path = np;
	id.track_idx = -1;
	id.value = p_value;
	id.type = Animation::TYPE_VALUE;
	id.query = vformat(TTR("property '%s'"), p_property);
	id.advance = false;
	// Dialog insert.
	_query_insert(id);
}

void AnimationTrackEditor::insert_value_key(const String &p_property, const Variant &p_value, bool p_advance) {
	EditorSelectionHistory *history = EditorNode::get_singleton()->get_editor_selection_history();

	ERR_FAIL_COND(!root);
	// Let's build a node path.
	ERR_FAIL_COND(history->get_path_size() == 0);
	Object *obj = ObjectDB::get_instance(history->get_path_object(0));
	ERR_FAIL_COND(!Object::cast_to<Node>(obj));

	Node *node = Object::cast_to<Node>(obj);

	String path = root->get_path_to(node);

	if (Object::cast_to<AnimationPlayer>(node) && p_property == "current_animation") {
		if (node == AnimationPlayerEditor::get_singleton()->get_player()) {
			EditorNode::get_singleton()->show_warning(TTR("AnimationPlayer can't animate itself, only other players."));
			return;
		}
		_insert_animation_key(path, p_value);
		return;
	}

	for (int i = 1; i < history->get_path_size(); i++) {
		String prop = history->get_path_property(i);
		ERR_FAIL_COND(prop.is_empty());
		path += ":" + prop;
	}

	path += ":" + p_property;

	NodePath np = path;

	// Locate track.

	bool inserted = false;

	make_insert_queue();
	for (int i = 0; i < animation->get_track_count(); i++) {
		if (animation->track_get_type(i) == Animation::TYPE_VALUE) {
			if (animation->track_get_path(i) != np) {
				continue;
			}

			InsertData id;
			id.path = np;
			id.track_idx = i;
			id.value = p_value;
			id.type = Animation::TYPE_VALUE;
			id.query = vformat(TTR("property '%s'"), p_property);
			id.advance = p_advance;
			// Dialog insert.
			_query_insert(id);
			inserted = true;
		} else if (animation->track_get_type(i) == Animation::TYPE_BEZIER) {
			Variant value;
			if (animation->track_get_path(i) == np) {
				value = p_value; // All good.
			} else {
				String tpath = animation->track_get_path(i);
				int index = tpath.rfind(":");
				if (NodePath(tpath.substr(0, index + 1)) == np) {
					String subindex = tpath.substr(index + 1, tpath.length() - index);
					value = p_value.get(subindex);
				} else {
					continue;
				}
			}

			InsertData id;
			id.path = animation->track_get_path(i);
			id.track_idx = i;
			id.value = value;
			id.type = Animation::TYPE_BEZIER;
			id.query = vformat(TTR("property '%s'"), p_property);
			id.advance = p_advance;
			// Dialog insert.
			_query_insert(id);
			inserted = true;
		}
	}
	commit_insert_queue();

	if (!inserted) {
		InsertData id;
		id.path = np;
		id.track_idx = -1;
		id.value = p_value;
		id.type = Animation::TYPE_VALUE;
		id.query = vformat(TTR("property '%s'"), p_property);
		id.advance = p_advance;
		// Dialog insert.
		_query_insert(id);
	}
}

Ref<Animation> AnimationTrackEditor::_create_and_get_reset_animation() {
	AnimationPlayer *player = AnimationPlayerEditor::get_singleton()->get_player();
	if (player->has_animation(SceneStringNames::get_singleton()->RESET)) {
		return player->get_animation(SceneStringNames::get_singleton()->RESET);
	} else {
		Ref<AnimationLibrary> al;
		if (!player->has_animation_library("")) {
			al.instantiate();
			player->add_animation_library("", al);
		} else {
			al = player->get_animation_library("");
		}

		Ref<Animation> reset_anim;
		reset_anim.instantiate();
		reset_anim->set_length(ANIM_MIN_LENGTH);
		undo_redo->add_do_method(al.ptr(), "add_animation", SceneStringNames::get_singleton()->RESET, reset_anim);
		undo_redo->add_do_method(AnimationPlayerEditor::get_singleton(), "_animation_player_changed", player);
		undo_redo->add_undo_method(al.ptr(), "remove_animation", SceneStringNames::get_singleton()->RESET);
		undo_redo->add_undo_method(AnimationPlayerEditor::get_singleton(), "_animation_player_changed", player);
		return reset_anim;
	}
}

void AnimationTrackEditor::_confirm_insert_list() {
	undo_redo->create_action(TTR("Anim Create & Insert"));

	bool create_reset = insert_confirm_reset->is_visible() && insert_confirm_reset->is_pressed();
	Ref<Animation> reset_anim;
	if (create_reset) {
		reset_anim = _create_and_get_reset_animation();
	}

	TrackIndices next_tracks(animation.ptr(), reset_anim.ptr());
	while (insert_data.size()) {
		next_tracks = _confirm_insert(insert_data.front()->get(), next_tracks, create_reset, reset_anim, insert_confirm_bezier->is_pressed());
		insert_data.pop_front();
	}

	undo_redo->commit_action();
}

PropertyInfo AnimationTrackEditor::_find_hint_for_track(int p_idx, NodePath &r_base_path, Variant *r_current_val) {
	r_base_path = NodePath();
	ERR_FAIL_COND_V(!animation.is_valid(), PropertyInfo());
	ERR_FAIL_INDEX_V(p_idx, animation->get_track_count(), PropertyInfo());

	if (!root) {
		return PropertyInfo();
	}

	NodePath path = animation->track_get_path(p_idx);

	if (!root->has_node_and_resource(path)) {
		return PropertyInfo();
	}

	Ref<Resource> res;
	Vector<StringName> leftover_path;
	Node *node = root->get_node_and_resource(path, res, leftover_path, true);

	if (node) {
		r_base_path = node->get_path();
	}

	if (leftover_path.is_empty()) {
		if (r_current_val) {
			if (res.is_valid()) {
				*r_current_val = res;
			} else if (node) {
				*r_current_val = node;
			}
		}
		return PropertyInfo();
	}

	Variant property_info_base;
	if (res.is_valid()) {
		property_info_base = res;
		if (r_current_val) {
			*r_current_val = res->get_indexed(leftover_path);
		}
	} else if (node) {
		property_info_base = node;
		if (r_current_val) {
			*r_current_val = node->get_indexed(leftover_path);
		}
	}

	for (int i = 0; i < leftover_path.size() - 1; i++) {
		bool valid;
		property_info_base = property_info_base.get_named(leftover_path[i], valid);
	}

	List<PropertyInfo> pinfo;
	property_info_base.get_property_list(&pinfo);

	for (const PropertyInfo &E : pinfo) {
		if (E.name == leftover_path[leftover_path.size() - 1]) {
			return E;
		}
	}

	return PropertyInfo();
}

static Vector<String> _get_bezier_subindices_for_type(Variant::Type p_type, bool *r_valid = nullptr) {
	Vector<String> subindices;
	if (r_valid) {
		*r_valid = true;
	}
	switch (p_type) {
		case Variant::INT: {
			subindices.push_back("");
		} break;
		case Variant::FLOAT: {
			subindices.push_back("");
		} break;
		case Variant::VECTOR2: {
			subindices.push_back(":x");
			subindices.push_back(":y");
		} break;
		case Variant::VECTOR3: {
			subindices.push_back(":x");
			subindices.push_back(":y");
			subindices.push_back(":z");
		} break;
		case Variant::QUATERNION: {
			subindices.push_back(":x");
			subindices.push_back(":y");
			subindices.push_back(":z");
			subindices.push_back(":w");
		} break;
		case Variant::COLOR: {
			subindices.push_back(":r");
			subindices.push_back(":g");
			subindices.push_back(":b");
			subindices.push_back(":a");
		} break;
		case Variant::PLANE: {
			subindices.push_back(":x");
			subindices.push_back(":y");
			subindices.push_back(":z");
			subindices.push_back(":d");
		} break;
		default: {
			if (r_valid) {
				*r_valid = false;
			}
		}
	}

	return subindices;
}

AnimationTrackEditor::TrackIndices AnimationTrackEditor::_confirm_insert(InsertData p_id, TrackIndices p_next_tracks, bool p_create_reset, Ref<Animation> p_reset_anim, bool p_create_beziers) {
	bool created = false;
	if (p_id.track_idx < 0) {
		if (p_create_beziers) {
			bool valid;
			Vector<String> subindices = _get_bezier_subindices_for_type(p_id.value.get_type(), &valid);
			if (valid) {
				for (int i = 0; i < subindices.size(); i++) {
					InsertData id = p_id;
					id.type = Animation::TYPE_BEZIER;
					id.value = p_id.value.get(subindices[i].substr(1, subindices[i].length()));
					id.path = String(p_id.path) + subindices[i];
					p_next_tracks = _confirm_insert(id, p_next_tracks, p_create_reset, p_reset_anim, false);
				}

				return p_next_tracks;
			}
		}
		created = true;
		undo_redo->create_action(TTR("Anim Insert Track & Key"));
		Animation::UpdateMode update_mode = Animation::UPDATE_DISCRETE;

		if (p_id.type == Animation::TYPE_VALUE || p_id.type == Animation::TYPE_BEZIER) {
			// Wants a new track.

			{
				// Hack.
				NodePath np;
				animation->add_track(p_id.type);
				animation->track_set_path(animation->get_track_count() - 1, p_id.path);
				PropertyInfo h = _find_hint_for_track(animation->get_track_count() - 1, np);
				animation->remove_track(animation->get_track_count() - 1); // Hack.

				if (h.type == Variant::FLOAT ||
						h.type == Variant::VECTOR2 ||
						h.type == Variant::RECT2 ||
						h.type == Variant::VECTOR3 ||
						h.type == Variant::AABB ||
						h.type == Variant::QUATERNION ||
						h.type == Variant::COLOR ||
						h.type == Variant::PLANE ||
						h.type == Variant::TRANSFORM2D ||
						h.type == Variant::TRANSFORM3D) {
					update_mode = Animation::UPDATE_CONTINUOUS;
				}

				if (h.usage & PROPERTY_USAGE_ANIMATE_AS_TRIGGER) {
					update_mode = Animation::UPDATE_TRIGGER;
				}
			}
		}

		p_id.track_idx = p_next_tracks.normal;

		undo_redo->add_do_method(animation.ptr(), "add_track", p_id.type);
		undo_redo->add_do_method(animation.ptr(), "track_set_path", p_id.track_idx, p_id.path);
		if (p_id.type == Animation::TYPE_VALUE) {
			undo_redo->add_do_method(animation.ptr(), "value_track_set_update_mode", p_id.track_idx, update_mode);
		}

	} else {
		undo_redo->create_action(TTR("Anim Insert Key"));
	}

	float time = timeline->get_play_position();
	Variant value;

	switch (p_id.type) {
		case Animation::TYPE_POSITION_3D:
		case Animation::TYPE_ROTATION_3D:
		case Animation::TYPE_SCALE_3D:
		case Animation::TYPE_BLEND_SHAPE:
		case Animation::TYPE_VALUE: {
			value = p_id.value;

		} break;
		case Animation::TYPE_BEZIER: {
			Array array;
			array.resize(6);
			array[0] = p_id.value;
			array[1] = -0.25;
			array[2] = 0;
			array[3] = 0.25;
			array[4] = 0;
			array[5] = Animation::HANDLE_MODE_BALANCED;
			value = array;
			bezier_edit_icon->set_disabled(false);

		} break;
		case Animation::TYPE_ANIMATION: {
			value = p_id.value;
		} break;
		default: {
		}
	}

	undo_redo->add_do_method(animation.ptr(), "track_insert_key", p_id.track_idx, time, value);

	if (created) {
		// Just remove the track.
		undo_redo->add_undo_method(this, "_clear_selection", false);
		undo_redo->add_undo_method(animation.ptr(), "remove_track", animation->get_track_count());
		p_next_tracks.normal++;
	} else {
		undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", p_id.track_idx, time);
		int existing = animation->track_find_key(p_id.track_idx, time, true);
		if (existing != -1) {
			Variant v = animation->track_get_key_value(p_id.track_idx, existing);
			float trans = animation->track_get_key_transition(p_id.track_idx, existing);
			undo_redo->add_undo_method(animation.ptr(), "track_insert_key", p_id.track_idx, time, v, trans);
		}
	}

	if (p_create_reset && track_type_is_resettable(p_id.type)) {
		bool create_reset_track = true;
		Animation *reset_anim = p_reset_anim.ptr();
		for (int i = 0; i < reset_anim->get_track_count(); i++) {
			if (reset_anim->track_get_path(i) == p_id.path) {
				create_reset_track = false;
				break;
			}
		}
		if (create_reset_track) {
			undo_redo->add_do_method(reset_anim, "add_track", p_id.type);
			undo_redo->add_do_method(reset_anim, "track_set_path", p_next_tracks.reset, p_id.path);
			undo_redo->add_do_method(reset_anim, "track_insert_key", p_next_tracks.reset, 0.0f, value);
			undo_redo->add_undo_method(reset_anim, "remove_track", reset_anim->get_track_count());
			p_next_tracks.reset++;
		}
	}

	undo_redo->commit_action();

	return p_next_tracks;
}

void AnimationTrackEditor::show_select_node_warning(bool p_show) {
	info_message->set_visible(p_show);
}

bool AnimationTrackEditor::is_key_selected(int p_track, int p_key) const {
	SelectedKey sk;
	sk.key = p_key;
	sk.track = p_track;

	return selection.has(sk);
}

bool AnimationTrackEditor::is_selection_active() const {
	return selection.size();
}

bool AnimationTrackEditor::is_snap_enabled() const {
	return snap->is_pressed() ^ Input::get_singleton()->is_key_pressed(Key::CTRL);
}

void AnimationTrackEditor::_update_tracks() {
	int selected = _get_track_selected();

	while (track_vbox->get_child_count()) {
		memdelete(track_vbox->get_child(0));
	}

	track_edits.clear();
	groups.clear();

	if (animation.is_null()) {
		return;
	}

	RBMap<String, VBoxContainer *> group_sort;

	bool use_grouping = !view_group->is_pressed();
	bool use_filter = selected_filter->is_pressed();

	for (int i = 0; i < animation->get_track_count(); i++) {
		AnimationTrackEdit *track_edit = nullptr;

		// Find hint and info for plugin.

		if (use_filter) {
			NodePath path = animation->track_get_path(i);

			if (root && root->has_node(path)) {
				Node *node = root->get_node(path);
				if (!node) {
					continue; // No node, no filter.
				}
				if (!EditorNode::get_singleton()->get_editor_selection()->is_selected(node)) {
					continue; // Skip track due to not selected.
				}
			}
		}

		if (animation->track_get_type(i) == Animation::TYPE_VALUE) {
			NodePath path = animation->track_get_path(i);

			if (root && root->has_node_and_resource(path)) {
				Ref<Resource> res;
				NodePath base_path;
				Vector<StringName> leftover_path;
				Node *node = root->get_node_and_resource(path, res, leftover_path, true);
				PropertyInfo pinfo = _find_hint_for_track(i, base_path);

				Object *object = node;
				if (res.is_valid()) {
					object = res.ptr();
				}

				if (object && !leftover_path.is_empty()) {
					if (pinfo.name.is_empty()) {
						pinfo.name = leftover_path[leftover_path.size() - 1];
					}

					for (int j = 0; j < track_edit_plugins.size(); j++) {
						track_edit = track_edit_plugins.write[j]->create_value_track_edit(object, pinfo.type, pinfo.name, pinfo.hint, pinfo.hint_string, pinfo.usage);
						if (track_edit) {
							break;
						}
					}
				}
			}
		}
		if (animation->track_get_type(i) == Animation::TYPE_AUDIO) {
			for (int j = 0; j < track_edit_plugins.size(); j++) {
				track_edit = track_edit_plugins.write[j]->create_audio_track_edit();
				if (track_edit) {
					break;
				}
			}
		}

		if (animation->track_get_type(i) == Animation::TYPE_ANIMATION) {
			NodePath path = animation->track_get_path(i);

			Node *node = nullptr;
			if (root && root->has_node(path)) {
				node = root->get_node(path);
			}

			if (node && Object::cast_to<AnimationPlayer>(node)) {
				for (int j = 0; j < track_edit_plugins.size(); j++) {
					track_edit = track_edit_plugins.write[j]->create_animation_track_edit(node);
					if (track_edit) {
						break;
					}
				}
			}
		}

		if (track_edit == nullptr) {
			// No valid plugin_found.
			track_edit = memnew(AnimationTrackEdit);
		}

		track_edits.push_back(track_edit);

		if (use_grouping) {
			String base_path = animation->track_get_path(i);
			base_path = base_path.get_slice(":", 0); // Remove sub-path.

			if (!group_sort.has(base_path)) {
				AnimationTrackEditGroup *g = memnew(AnimationTrackEditGroup);
				Ref<Texture2D> icon = get_theme_icon(SNAME("Node"), SNAME("EditorIcons"));
				String name = base_path;
				String tooltip;
				if (root && root->has_node(base_path)) {
					Node *n = root->get_node(base_path);
					if (n) {
						icon = EditorNode::get_singleton()->get_object_icon(n, "Node");
						name = n->get_name();
						tooltip = root->get_path_to(n);
					}
				}

				g->set_type_and_name(icon, name, animation->track_get_path(i));
				g->set_root(root);
				g->set_tooltip(tooltip);
				g->set_timeline(timeline);
				groups.push_back(g);
				VBoxContainer *vb = memnew(VBoxContainer);
				vb->add_theme_constant_override("separation", 0);
				vb->add_child(g);
				track_vbox->add_child(vb);
				group_sort[base_path] = vb;
			}

			track_edit->set_in_group(true);
			group_sort[base_path]->add_child(track_edit);

		} else {
			track_edit->set_in_group(false);
			track_vbox->add_child(track_edit);
		}

		track_edit->set_undo_redo(undo_redo);
		track_edit->set_timeline(timeline);
		track_edit->set_root(root);
		track_edit->set_animation_and_track(animation, i);
		track_edit->set_play_position(timeline->get_play_position());
		track_edit->set_editor(this);

		if (selected == i) {
			track_edit->grab_focus();
		}

		track_edit->connect("timeline_changed", callable_mp(this, &AnimationTrackEditor::_timeline_changed));
		track_edit->connect("remove_request", callable_mp(this, &AnimationTrackEditor::_track_remove_request), varray(), CONNECT_DEFERRED);
		track_edit->connect("dropped", callable_mp(this, &AnimationTrackEditor::_dropped_track), varray(), CONNECT_DEFERRED);
		track_edit->connect("insert_key", callable_mp(this, &AnimationTrackEditor::_insert_key_from_track), varray(i), CONNECT_DEFERRED);
		track_edit->connect("select_key", callable_mp(this, &AnimationTrackEditor::_key_selected), varray(i), CONNECT_DEFERRED);
		track_edit->connect("deselect_key", callable_mp(this, &AnimationTrackEditor::_key_deselected), varray(i), CONNECT_DEFERRED);
		track_edit->connect("move_selection_begin", callable_mp(this, &AnimationTrackEditor::_move_selection_begin));
		track_edit->connect("move_selection", callable_mp(this, &AnimationTrackEditor::_move_selection));
		track_edit->connect("move_selection_commit", callable_mp(this, &AnimationTrackEditor::_move_selection_commit));
		track_edit->connect("move_selection_cancel", callable_mp(this, &AnimationTrackEditor::_move_selection_cancel));

		track_edit->connect("duplicate_request", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed), varray(EDIT_DUPLICATE_SELECTION), CONNECT_DEFERRED);
		track_edit->connect("duplicate_transpose_request", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed), varray(EDIT_DUPLICATE_TRANSPOSED), CONNECT_DEFERRED);
		track_edit->connect("create_reset_request", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed), varray(EDIT_ADD_RESET_KEY), CONNECT_DEFERRED);
		track_edit->connect("delete_request", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed), varray(EDIT_DELETE_SELECTION), CONNECT_DEFERRED);
	}
}

void AnimationTrackEditor::_animation_changed() {
	if (animation_changing_awaiting_update) {
		return; // All will be updated, don't bother with anything.
	}

	if (key_edit && key_edit->setting) {
		// If editing a key, just update the edited track, makes refresh less costly.
		if (key_edit->track < track_edits.size()) {
			if (animation->track_get_type(key_edit->track) == Animation::TYPE_BEZIER) {
				bezier_edit->update();
			} else {
				track_edits[key_edit->track]->update();
			}
		}
		return;
	}

	animation_changing_awaiting_update = true;
	call_deferred(SNAME("_animation_update"));
}

void AnimationTrackEditor::_snap_mode_changed(int p_mode) {
	timeline->set_use_fps(p_mode == 1);
	if (key_edit) {
		key_edit->set_use_fps(p_mode == 1);
	}
	_update_step_spinbox();
}

void AnimationTrackEditor::_update_step_spinbox() {
	if (!animation.is_valid()) {
		return;
	}
	step->set_block_signals(true);

	if (timeline->is_using_fps()) {
		if (animation->get_step() == 0) {
			step->set_value(0);
		} else {
			step->set_value(1.0 / animation->get_step());
		}

	} else {
		step->set_value(animation->get_step());
	}

	step->set_block_signals(false);
}

void AnimationTrackEditor::_animation_update() {
	timeline->update();
	timeline->update_values();

	bool same = true;

	if (animation.is_null()) {
		return;
	}

	if (track_edits.size() == animation->get_track_count()) {
		// Check tracks are the same.

		for (int i = 0; i < track_edits.size(); i++) {
			if (track_edits[i]->get_path() != animation->track_get_path(i)) {
				same = false;
				break;
			}
		}
	} else {
		same = false;
	}

	if (same) {
		for (int i = 0; i < track_edits.size(); i++) {
			track_edits[i]->update();
		}
		for (int i = 0; i < groups.size(); i++) {
			groups[i]->update();
		}
	} else {
		_update_tracks();
	}

	bezier_edit->update();

	_update_step_spinbox();
	emit_signal(SNAME("animation_step_changed"), animation->get_step());
	emit_signal(SNAME("animation_len_changed"), animation->get_length());

	animation_changing_awaiting_update = false;
}

MenuButton *AnimationTrackEditor::get_edit_menu() {
	return edit;
}

void AnimationTrackEditor::_notification(int p_what) {
	switch (p_what) {
		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			panner->setup((ViewPanner::ControlScheme)EDITOR_GET("editors/panning/animation_editors_panning_scheme").operator int(), ED_GET_SHORTCUT("canvas_item_editor/pan_view"), bool(EditorSettings::get_singleton()->get("editors/panning/simple_panning")));
		} break;

		case NOTIFICATION_ENTER_TREE: {
			panner->setup((ViewPanner::ControlScheme)EDITOR_GET("editors/panning/animation_editors_panning_scheme").operator int(), ED_GET_SHORTCUT("canvas_item_editor/pan_view"), bool(EditorSettings::get_singleton()->get("editors/panning/simple_panning")));
			[[fallthrough]];
		}
		case NOTIFICATION_THEME_CHANGED: {
			zoom_icon->set_texture(get_theme_icon(SNAME("Zoom"), SNAME("EditorIcons")));
			bezier_edit_icon->set_icon(get_theme_icon(SNAME("EditBezier"), SNAME("EditorIcons")));
			snap->set_icon(get_theme_icon(SNAME("Snap"), SNAME("EditorIcons")));
			view_group->set_icon(get_theme_icon(view_group->is_pressed() ? SNAME("AnimationTrackList") : SNAME("AnimationTrackGroup"), SNAME("EditorIcons")));
			selected_filter->set_icon(get_theme_icon(SNAME("AnimationFilter"), SNAME("EditorIcons")));
			imported_anim_warning->set_icon(get_theme_icon(SNAME("NodeWarning"), SNAME("EditorIcons")));
			main_panel->add_theme_style_override("panel", get_theme_stylebox(SNAME("bg"), SNAME("Tree")));
			edit->get_popup()->set_item_icon(edit->get_popup()->get_item_index(EDIT_APPLY_RESET), get_theme_icon(SNAME("Reload"), SNAME("EditorIcons")));
		} break;

		case NOTIFICATION_READY: {
			EditorNode::get_singleton()->get_editor_selection()->connect("selection_changed", callable_mp(this, &AnimationTrackEditor::_selection_changed));
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			update_keying();
		} break;
	}
}

void AnimationTrackEditor::_update_scroll(double) {
	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->update();
	}
	for (int i = 0; i < groups.size(); i++) {
		groups[i]->update();
	}
}

void AnimationTrackEditor::_update_step(double p_new_step) {
	undo_redo->create_action(TTR("Change Animation Step"));
	float step_value = p_new_step;
	if (timeline->is_using_fps()) {
		if (step_value != 0.0) {
			step_value = 1.0 / step_value;
		}
	}
	undo_redo->add_do_method(animation.ptr(), "set_step", step_value);
	undo_redo->add_undo_method(animation.ptr(), "set_step", animation->get_step());
	step->set_block_signals(true);
	undo_redo->commit_action();
	step->set_block_signals(false);
	emit_signal(SNAME("animation_step_changed"), step_value);
}

void AnimationTrackEditor::_update_length(double p_new_len) {
	emit_signal(SNAME("animation_len_changed"), p_new_len);
}

void AnimationTrackEditor::_dropped_track(int p_from_track, int p_to_track) {
	if (p_from_track == p_to_track || p_from_track == p_to_track - 1) {
		return;
	}

	_clear_selection();
	undo_redo->create_action(TTR("Rearrange Tracks"));
	undo_redo->add_do_method(animation.ptr(), "track_move_to", p_from_track, p_to_track);
	// Take into account that the position of the tracks that come after the one removed will change.
	int to_track_real = p_to_track > p_from_track ? p_to_track - 1 : p_to_track;
	undo_redo->add_undo_method(animation.ptr(), "track_move_to", to_track_real, p_to_track > p_from_track ? p_from_track : p_from_track + 1);
	undo_redo->add_do_method(this, "_track_grab_focus", to_track_real);
	undo_redo->add_undo_method(this, "_track_grab_focus", p_from_track);
	undo_redo->commit_action();
}

void AnimationTrackEditor::_new_track_node_selected(NodePath p_path) {
	ERR_FAIL_COND(!root);
	Node *node = get_node(p_path);
	ERR_FAIL_COND(!node);
	NodePath path_to = root->get_path_to(node);

	if (adding_track_type == Animation::TYPE_BLEND_SHAPE && !node->is_class("MeshInstance3D")) {
		EditorNode::get_singleton()->show_warning(TTR("Blend Shape tracks only apply to MeshInstance3D nodes."));
		return;
	}

	if ((adding_track_type == Animation::TYPE_POSITION_3D || adding_track_type == Animation::TYPE_ROTATION_3D || adding_track_type == Animation::TYPE_SCALE_3D) && !node->is_class("Node3D")) {
		EditorNode::get_singleton()->show_warning(TTR("Position/Rotation/Scale 3D tracks only apply to 3D-based nodes."));
		return;
	}

	switch (adding_track_type) {
		case Animation::TYPE_VALUE: {
			adding_track_path = path_to;
			prop_selector->set_type_filter(Vector<Variant::Type>());
			prop_selector->select_property_from_instance(node);
		} break;
		case Animation::TYPE_BLEND_SHAPE: {
			adding_track_path = path_to;
			Vector<Variant::Type> filter;
			filter.push_back(Variant::FLOAT);
			prop_selector->set_type_filter(filter);
			prop_selector->select_property_from_instance(node);
		} break;
		case Animation::TYPE_POSITION_3D:
		case Animation::TYPE_ROTATION_3D:
		case Animation::TYPE_SCALE_3D:
		case Animation::TYPE_METHOD: {
			undo_redo->create_action(TTR("Add Track"));
			undo_redo->add_do_method(animation.ptr(), "add_track", adding_track_type);
			undo_redo->add_do_method(animation.ptr(), "track_set_path", animation->get_track_count(), path_to);
			undo_redo->add_undo_method(animation.ptr(), "remove_track", animation->get_track_count());
			undo_redo->commit_action();

		} break;
		case Animation::TYPE_BEZIER: {
			Vector<Variant::Type> filter;
			filter.push_back(Variant::INT);
			filter.push_back(Variant::FLOAT);
			filter.push_back(Variant::VECTOR2);
			filter.push_back(Variant::VECTOR3);
			filter.push_back(Variant::QUATERNION);
			filter.push_back(Variant::PLANE);
			filter.push_back(Variant::COLOR);

			adding_track_path = path_to;
			prop_selector->set_type_filter(filter);
			prop_selector->select_property_from_instance(node);
			bezier_edit_icon->set_disabled(false);
		} break;
		case Animation::TYPE_AUDIO: {
			if (!node->is_class("AudioStreamPlayer") && !node->is_class("AudioStreamPlayer2D") && !node->is_class("AudioStreamPlayer3D")) {
				EditorNode::get_singleton()->show_warning(TTR("Audio tracks can only point to nodes of type:\n-AudioStreamPlayer\n-AudioStreamPlayer2D\n-AudioStreamPlayer3D"));
				return;
			}

			undo_redo->create_action(TTR("Add Track"));
			undo_redo->add_do_method(animation.ptr(), "add_track", adding_track_type);
			undo_redo->add_do_method(animation.ptr(), "track_set_path", animation->get_track_count(), path_to);
			undo_redo->add_undo_method(animation.ptr(), "remove_track", animation->get_track_count());
			undo_redo->commit_action();

		} break;
		case Animation::TYPE_ANIMATION: {
			if (!node->is_class("AnimationPlayer")) {
				EditorNode::get_singleton()->show_warning(TTR("Animation tracks can only point to AnimationPlayer nodes."));
				return;
			}

			if (node == AnimationPlayerEditor::get_singleton()->get_player()) {
				EditorNode::get_singleton()->show_warning(TTR("AnimationPlayer can't animate itself, only other players."));
				return;
			}

			undo_redo->create_action(TTR("Add Track"));
			undo_redo->add_do_method(animation.ptr(), "add_track", adding_track_type);
			undo_redo->add_do_method(animation.ptr(), "track_set_path", animation->get_track_count(), path_to);
			undo_redo->add_undo_method(animation.ptr(), "remove_track", animation->get_track_count());
			undo_redo->commit_action();

		} break;
	}
}

void AnimationTrackEditor::_add_track(int p_type) {
	if (!root) {
		EditorNode::get_singleton()->show_warning(TTR("Not possible to add a new track without a root"));
		return;
	}
	adding_track_type = p_type;
	pick_track->popup_scenetree_dialog();
	pick_track->get_filter_line_edit()->clear();
	pick_track->get_filter_line_edit()->grab_focus();
}

void AnimationTrackEditor::_new_track_property_selected(String p_name) {
	String full_path = String(adding_track_path) + ":" + p_name;

	if (adding_track_type == Animation::TYPE_VALUE) {
		Animation::UpdateMode update_mode = Animation::UPDATE_DISCRETE;
		{
			// Hack.
			NodePath np;
			animation->add_track(Animation::TYPE_VALUE);
			animation->track_set_path(animation->get_track_count() - 1, full_path);
			PropertyInfo h = _find_hint_for_track(animation->get_track_count() - 1, np);
			animation->remove_track(animation->get_track_count() - 1); // Hack.
			if (h.type == Variant::FLOAT ||
					h.type == Variant::VECTOR2 ||
					h.type == Variant::RECT2 ||
					h.type == Variant::VECTOR3 ||
					h.type == Variant::AABB ||
					h.type == Variant::QUATERNION ||
					h.type == Variant::COLOR ||
					h.type == Variant::PLANE ||
					h.type == Variant::TRANSFORM2D ||
					h.type == Variant::TRANSFORM3D) {
				update_mode = Animation::UPDATE_CONTINUOUS;
			}

			if (h.usage & PROPERTY_USAGE_ANIMATE_AS_TRIGGER) {
				update_mode = Animation::UPDATE_TRIGGER;
			}
		}

		undo_redo->create_action(TTR("Add Track"));
		undo_redo->add_do_method(animation.ptr(), "add_track", adding_track_type);
		undo_redo->add_do_method(animation.ptr(), "track_set_path", animation->get_track_count(), full_path);
		undo_redo->add_do_method(animation.ptr(), "value_track_set_update_mode", animation->get_track_count(), update_mode);
		undo_redo->add_undo_method(animation.ptr(), "remove_track", animation->get_track_count());
		undo_redo->commit_action();
	} else {
		Vector<String> subindices;
		{
			// Hack.
			NodePath np;
			animation->add_track(Animation::TYPE_VALUE);
			animation->track_set_path(animation->get_track_count() - 1, full_path);
			PropertyInfo h = _find_hint_for_track(animation->get_track_count() - 1, np);
			animation->remove_track(animation->get_track_count() - 1); // Hack.
			bool valid;
			subindices = _get_bezier_subindices_for_type(h.type, &valid);
			if (!valid) {
				EditorNode::get_singleton()->show_warning(TTR("Invalid track for Bezier (no suitable sub-properties)"));
				return;
			}
		}

		undo_redo->create_action(TTR("Add Bezier Track"));
		int base_track = animation->get_track_count();
		for (int i = 0; i < subindices.size(); i++) {
			undo_redo->add_do_method(animation.ptr(), "add_track", adding_track_type);
			undo_redo->add_do_method(animation.ptr(), "track_set_path", base_track + i, full_path + subindices[i]);
			undo_redo->add_undo_method(animation.ptr(), "remove_track", base_track);
		}
		undo_redo->commit_action();
	}
}

void AnimationTrackEditor::_timeline_value_changed(double) {
	timeline->update_play_position();

	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->update();
		track_edits[i]->update_play_position();
	}

	for (int i = 0; i < groups.size(); i++) {
		groups[i]->update();
	}

	bezier_edit->update();
	bezier_edit->update_play_position();
}

int AnimationTrackEditor::_get_track_selected() {
	for (int i = 0; i < track_edits.size(); i++) {
		if (track_edits[i]->has_focus()) {
			return i;
		}
	}

	return -1;
}

void AnimationTrackEditor::_insert_key_from_track(float p_ofs, int p_track) {
	ERR_FAIL_INDEX(p_track, animation->get_track_count());

	if (snap->is_pressed() && step->get_value() != 0) {
		p_ofs = snap_time(p_ofs);
	}
	while (animation->track_find_key(p_track, p_ofs, true) != -1) { // Make sure insertion point is valid.
		p_ofs += 0.001;
	}

	switch (animation->track_get_type(p_track)) {
		case Animation::TYPE_POSITION_3D: {
			if (!root->has_node(animation->track_get_path(p_track))) {
				EditorNode::get_singleton()->show_warning(TTR("Track path is invalid, so can't add a key."));
				return;
			}
			Node3D *base = Object::cast_to<Node3D>(root->get_node(animation->track_get_path(p_track)));

			if (!base) {
				EditorNode::get_singleton()->show_warning(TTR("Track is not of type Node3D, can't insert key"));
				return;
			}

			Vector3 pos = base->get_position();

			undo_redo->create_action(TTR("Add Position Key"));
			undo_redo->add_do_method(animation.ptr(), "position_track_insert_key", p_track, p_ofs, pos);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", p_track, p_ofs);
			undo_redo->commit_action();

		} break;
		case Animation::TYPE_ROTATION_3D: {
			if (!root->has_node(animation->track_get_path(p_track))) {
				EditorNode::get_singleton()->show_warning(TTR("Track path is invalid, so can't add a key."));
				return;
			}
			Node3D *base = Object::cast_to<Node3D>(root->get_node(animation->track_get_path(p_track)));

			if (!base) {
				EditorNode::get_singleton()->show_warning(TTR("Track is not of type Node3D, can't insert key"));
				return;
			}

			Quaternion rot = base->get_transform().basis.operator Quaternion();

			undo_redo->create_action(TTR("Add Rotation Key"));
			undo_redo->add_do_method(animation.ptr(), "rotation_track_insert_key", p_track, p_ofs, rot);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", p_track, p_ofs);
			undo_redo->commit_action();

		} break;
		case Animation::TYPE_SCALE_3D: {
			if (!root->has_node(animation->track_get_path(p_track))) {
				EditorNode::get_singleton()->show_warning(TTR("Track path is invalid, so can't add a key."));
				return;
			}
			Node3D *base = Object::cast_to<Node3D>(root->get_node(animation->track_get_path(p_track)));

			if (!base) {
				EditorNode::get_singleton()->show_warning(TTR("Track is not of type Node3D, can't insert key"));
				return;
			}

			Vector3 scale = base->get_scale();

			undo_redo->create_action(TTR("Add Scale Key"));
			undo_redo->add_do_method(animation.ptr(), "scale_track_insert_key", p_track, p_ofs, scale);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", p_track, p_ofs);
			undo_redo->commit_action();

		} break;
		case Animation::TYPE_BLEND_SHAPE:
		case Animation::TYPE_VALUE: {
			NodePath bp;
			Variant value;
			_find_hint_for_track(p_track, bp, &value);

			undo_redo->create_action(TTR("Add Track Key"));
			undo_redo->add_do_method(animation.ptr(), "track_insert_key", p_track, p_ofs, value);
			undo_redo->add_undo_method(this, "_clear_selection_for_anim", animation);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", p_track, p_ofs);
			undo_redo->commit_action();

		} break;
		case Animation::TYPE_METHOD: {
			if (!root->has_node(animation->track_get_path(p_track))) {
				EditorNode::get_singleton()->show_warning(TTR("Track path is invalid, so can't add a method key."));
				return;
			}
			Node *base = root->get_node(animation->track_get_path(p_track));

			method_selector->select_method_from_instance(base);

			insert_key_from_track_call_ofs = p_ofs;
			insert_key_from_track_call_track = p_track;

		} break;
		case Animation::TYPE_BEZIER: {
			NodePath bp;
			Variant value;
			_find_hint_for_track(p_track, bp, &value);
			Array arr;
			arr.resize(6);
			arr[0] = value;
			arr[1] = -0.25;
			arr[2] = 0;
			arr[3] = 0.25;
			arr[4] = 0;
			arr[5] = 0;

			undo_redo->create_action(TTR("Add Track Key"));
			undo_redo->add_do_method(animation.ptr(), "track_insert_key", p_track, p_ofs, arr);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", p_track, p_ofs);
			undo_redo->commit_action();

		} break;
		case Animation::TYPE_AUDIO: {
			Dictionary ak;
			ak["stream"] = Ref<Resource>();
			ak["start_offset"] = 0;
			ak["end_offset"] = 0;

			undo_redo->create_action(TTR("Add Track Key"));
			undo_redo->add_do_method(animation.ptr(), "track_insert_key", p_track, p_ofs, ak);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", p_track, p_ofs);
			undo_redo->commit_action();
		} break;
		case Animation::TYPE_ANIMATION: {
			StringName anim = "[stop]";

			undo_redo->create_action(TTR("Add Track Key"));
			undo_redo->add_do_method(animation.ptr(), "track_insert_key", p_track, p_ofs, anim);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", p_track, p_ofs);
			undo_redo->commit_action();
		} break;
	}
}

void AnimationTrackEditor::_add_method_key(const String &p_method) {
	if (!root->has_node(animation->track_get_path(insert_key_from_track_call_track))) {
		EditorNode::get_singleton()->show_warning(TTR("Track path is invalid, so can't add a method key."));
		return;
	}
	Node *base = root->get_node(animation->track_get_path(insert_key_from_track_call_track));

	List<MethodInfo> minfo;
	base->get_method_list(&minfo);

	for (const MethodInfo &E : minfo) {
		if (E.name == p_method) {
			Dictionary d;
			d["method"] = p_method;
			Array params;
			int first_defarg = E.arguments.size() - E.default_arguments.size();

			for (int i = 0; i < E.arguments.size(); i++) {
				if (i >= first_defarg) {
					Variant arg = E.default_arguments[i - first_defarg];
					params.push_back(arg);
				} else {
					Callable::CallError ce;
					Variant arg;
					Variant::construct(E.arguments[i].type, arg, nullptr, 0, ce);
					params.push_back(arg);
				}
			}
			d["args"] = params;

			undo_redo->create_action(TTR("Add Method Track Key"));
			undo_redo->add_do_method(animation.ptr(), "track_insert_key", insert_key_from_track_call_track, insert_key_from_track_call_ofs, d);
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", insert_key_from_track_call_track, insert_key_from_track_call_ofs);
			undo_redo->commit_action();

			return;
		}
	}

	EditorNode::get_singleton()->show_warning(TTR("Method not found in object: ") + p_method);
}

void AnimationTrackEditor::_key_selected(int p_key, bool p_single, int p_track) {
	ERR_FAIL_INDEX(p_track, animation->get_track_count());
	ERR_FAIL_INDEX(p_key, animation->track_get_key_count(p_track));

	SelectedKey sk;
	sk.key = p_key;
	sk.track = p_track;

	if (p_single) {
		_clear_selection();
	}

	KeyInfo ki;
	ki.pos = animation->track_get_key_time(p_track, p_key);
	selection[sk] = ki;

	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->update();
	}

	_update_key_edit();
}

void AnimationTrackEditor::_key_deselected(int p_key, int p_track) {
	ERR_FAIL_INDEX(p_track, animation->get_track_count());
	ERR_FAIL_INDEX(p_key, animation->track_get_key_count(p_track));

	SelectedKey sk;
	sk.key = p_key;
	sk.track = p_track;

	selection.erase(sk);

	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->update();
	}

	_update_key_edit();
}

void AnimationTrackEditor::_move_selection_begin() {
	moving_selection = true;
	moving_selection_offset = 0;
}

void AnimationTrackEditor::_move_selection(float p_offset) {
	moving_selection_offset = p_offset;

	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->update();
	}
}

struct _AnimMoveRestore {
	int track = 0;
	float time = 0;
	Variant key;
	float transition = 0;
};
// Used for undo/redo.

void AnimationTrackEditor::_clear_key_edit() {
	if (key_edit) {
		// If key edit is the object being inspected, remove it first.
		if (InspectorDock::get_inspector_singleton()->get_edited_object() == key_edit) {
			EditorNode::get_singleton()->push_item(nullptr);
		}

		// Then actually delete it.
		memdelete(key_edit);
		key_edit = nullptr;
	}

	if (multi_key_edit) {
		if (InspectorDock::get_inspector_singleton()->get_edited_object() == multi_key_edit) {
			EditorNode::get_singleton()->push_item(nullptr);
		}

		memdelete(multi_key_edit);
		multi_key_edit = nullptr;
	}
}

void AnimationTrackEditor::_clear_selection(bool p_update) {
	selection.clear();

	if (p_update) {
		for (int i = 0; i < track_edits.size(); i++) {
			track_edits[i]->update();
		}
	}

	_clear_key_edit();
}

void AnimationTrackEditor::_update_key_edit() {
	_clear_key_edit();
	if (!animation.is_valid()) {
		return;
	}

	if (selection.size() == 1) {
		key_edit = memnew(AnimationTrackKeyEdit);
		key_edit->animation = animation;
		key_edit->track = selection.front()->key().track;
		key_edit->use_fps = timeline->is_using_fps();

		float ofs = animation->track_get_key_time(key_edit->track, selection.front()->key().key);
		key_edit->key_ofs = ofs;
		key_edit->root_path = root;

		NodePath np;
		key_edit->hint = _find_hint_for_track(key_edit->track, np);
		key_edit->undo_redo = undo_redo;
		key_edit->base = np;

		EditorNode::get_singleton()->push_item(key_edit);
	} else if (selection.size() > 1) {
		multi_key_edit = memnew(AnimationMultiTrackKeyEdit);
		multi_key_edit->animation = animation;

		RBMap<int, List<float>> key_ofs_map;
		RBMap<int, NodePath> base_map;
		int first_track = -1;
		for (const KeyValue<SelectedKey, KeyInfo> &E : selection) {
			int track = E.key.track;
			if (first_track < 0) {
				first_track = track;
			}

			if (!key_ofs_map.has(track)) {
				key_ofs_map[track] = List<float>();
				base_map[track] = NodePath();
			}

			key_ofs_map[track].push_back(animation->track_get_key_time(track, E.key.key));
		}
		multi_key_edit->key_ofs_map = key_ofs_map;
		multi_key_edit->base_map = base_map;
		multi_key_edit->hint = _find_hint_for_track(first_track, base_map[first_track]);

		multi_key_edit->use_fps = timeline->is_using_fps();

		multi_key_edit->root_path = root;

		multi_key_edit->undo_redo = undo_redo;

		EditorNode::get_singleton()->push_item(multi_key_edit);
	}
}

void AnimationTrackEditor::_clear_selection_for_anim(const Ref<Animation> &p_anim) {
	if (animation != p_anim) {
		return;
	}

	_clear_selection();
}

void AnimationTrackEditor::_select_at_anim(const Ref<Animation> &p_anim, int p_track, float p_pos) {
	if (animation != p_anim) {
		return;
	}

	int idx = animation->track_find_key(p_track, p_pos, true);
	ERR_FAIL_COND(idx < 0);

	SelectedKey sk;
	sk.track = p_track;
	sk.key = idx;
	KeyInfo ki;
	ki.pos = p_pos;

	selection.insert(sk, ki);
}

void AnimationTrackEditor::_move_selection_commit() {
	undo_redo->create_action(TTR("Anim Move Keys"));

	List<_AnimMoveRestore> to_restore;

	float motion = moving_selection_offset;
	// 1 - remove the keys.
	for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
		undo_redo->add_do_method(animation.ptr(), "track_remove_key", E->key().track, E->key().key);
	}
	// 2 - Remove overlapped keys.
	for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
		float newtime = snap_time(E->get().pos + motion);
		int idx = animation->track_find_key(E->key().track, newtime, true);
		if (idx == -1) {
			continue;
		}
		SelectedKey sk;
		sk.key = idx;
		sk.track = E->key().track;
		if (selection.has(sk)) {
			continue; // Already in selection, don't save.
		}

		undo_redo->add_do_method(animation.ptr(), "track_remove_key_at_time", E->key().track, newtime);
		_AnimMoveRestore amr;

		amr.key = animation->track_get_key_value(E->key().track, idx);
		amr.track = E->key().track;
		amr.time = newtime;
		amr.transition = animation->track_get_key_transition(E->key().track, idx);

		to_restore.push_back(amr);
	}

	// 3 - Move the keys (Reinsert them).
	for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
		float newpos = snap_time(E->get().pos + motion);
		undo_redo->add_do_method(animation.ptr(), "track_insert_key", E->key().track, newpos, animation->track_get_key_value(E->key().track, E->key().key), animation->track_get_key_transition(E->key().track, E->key().key));
	}

	// 4 - (Undo) Remove inserted keys.
	for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
		float newpos = snap_time(E->get().pos + motion);
		undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", E->key().track, newpos);
	}

	// 5 - (Undo) Reinsert keys.
	for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
		undo_redo->add_undo_method(animation.ptr(), "track_insert_key", E->key().track, E->get().pos, animation->track_get_key_value(E->key().track, E->key().key), animation->track_get_key_transition(E->key().track, E->key().key));
	}

	// 6 - (Undo) Reinsert overlapped keys.
	for (_AnimMoveRestore &amr : to_restore) {
		undo_redo->add_undo_method(animation.ptr(), "track_insert_key", amr.track, amr.time, amr.key, amr.transition);
	}

	undo_redo->add_do_method(this, "_clear_selection_for_anim", animation);
	undo_redo->add_undo_method(this, "_clear_selection_for_anim", animation);

	// 7 - Reselect.
	for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
		float oldpos = E->get().pos;
		float newpos = snap_time(oldpos + motion);

		undo_redo->add_do_method(this, "_select_at_anim", animation, E->key().track, newpos);
		undo_redo->add_undo_method(this, "_select_at_anim", animation, E->key().track, oldpos);
	}

	undo_redo->commit_action();

	moving_selection = false;
	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->update();
	}

	_update_key_edit();
}

void AnimationTrackEditor::_move_selection_cancel() {
	moving_selection = false;
	for (int i = 0; i < track_edits.size(); i++) {
		track_edits[i]->update();
	}
}

bool AnimationTrackEditor::is_moving_selection() const {
	return moving_selection;
}

float AnimationTrackEditor::get_moving_selection_offset() const {
	return moving_selection_offset;
}

void AnimationTrackEditor::_box_selection_draw() {
	const Rect2 selection_rect = Rect2(Point2(), box_selection->get_size());
	box_selection->draw_rect(selection_rect, get_theme_color(SNAME("box_selection_fill_color"), SNAME("Editor")));
	box_selection->draw_rect(selection_rect, get_theme_color(SNAME("box_selection_stroke_color"), SNAME("Editor")), false, Math::round(EDSCALE));
}

void AnimationTrackEditor::_scroll_input(const Ref<InputEvent> &p_event) {
	if (panner->gui_input(p_event)) {
		scroll->accept_event();
		return;
	}

	Ref<InputEventMouseButton> mb = p_event;

	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			box_selecting = true;
			box_selecting_from = scroll->get_global_transform().xform(mb->get_position());
			box_select_rect = Rect2();
		} else if (box_selecting) {
			if (box_selection->is_visible_in_tree()) {
				// Only if moved.
				for (int i = 0; i < track_edits.size(); i++) {
					Rect2 local_rect = box_select_rect;
					local_rect.position -= track_edits[i]->get_global_position();
					track_edits[i]->append_to_selection(local_rect, mb->is_command_pressed());
				}

				if (_get_track_selected() == -1 && track_edits.size() > 0) { // Minimal hack to make shortcuts work.
					track_edits[track_edits.size() - 1]->grab_focus();
				}
			} else {
				_clear_selection(); // Clear it.
			}

			box_selection->hide();
			box_selecting = false;
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;

	if (mm.is_valid() && box_selecting) {
		if ((mm->get_button_mask() & MouseButton::MASK_LEFT) == MouseButton::NONE) {
			// No longer.
			box_selection->hide();
			box_selecting = false;
			return;
		}

		if (!box_selection->is_visible_in_tree()) {
			if (!mm->is_command_pressed() && !mm->is_shift_pressed()) {
				_clear_selection();
			}
			box_selection->show();
		}

		Vector2 from = box_selecting_from;
		Vector2 to = scroll->get_global_transform().xform(mm->get_position());

		if (from.x > to.x) {
			SWAP(from.x, to.x);
		}

		if (from.y > to.y) {
			SWAP(from.y, to.y);
		}

		Rect2 rect(from, to - from);
		Rect2 scroll_rect = Rect2(scroll->get_global_position(), scroll->get_size());
		rect = scroll_rect.intersection(rect);
		box_selection->set_position(rect.position);
		box_selection->set_size(rect.size);

		box_select_rect = rect;
	}
}

void AnimationTrackEditor::_toggle_bezier_edit() {
	if (bezier_edit->is_visible()) {
		_cancel_bezier_edit();
	} else {
		int track_count = animation->get_track_count();
		for (int i = 0; i < track_count; ++i) {
			if (animation->track_get_type(i) == Animation::TrackType::TYPE_BEZIER) {
				_bezier_edit(i);
				return;
			}
		}
	}
}

void AnimationTrackEditor::_scroll_callback(Vector2 p_scroll_vec, bool p_alt) {
	if (p_alt) {
		if (p_scroll_vec.x < 0 || p_scroll_vec.y < 0) {
			goto_prev_step(true);
		} else {
			goto_next_step(true);
		}
	} else {
		_pan_callback(-p_scroll_vec * 32);
	}
}

void AnimationTrackEditor::_pan_callback(Vector2 p_scroll_vec) {
	timeline->set_value(timeline->get_value() - p_scroll_vec.x / timeline->get_zoom_scale());
	scroll->set_v_scroll(scroll->get_v_scroll() - p_scroll_vec.y);
}

void AnimationTrackEditor::_zoom_callback(Vector2 p_scroll_vec, Vector2 p_origin, bool p_alt) {
	double new_zoom_value;
	double current_zoom_value = timeline->get_zoom()->get_value();
	if (current_zoom_value <= 0.1) {
		new_zoom_value = MAX(0.01, current_zoom_value - 0.01 * SIGN(p_scroll_vec.y));
	} else {
		new_zoom_value = p_scroll_vec.y > 0 ? MAX(0.01, current_zoom_value / 1.05) : current_zoom_value * 1.05;
	}
	timeline->get_zoom()->set_value(new_zoom_value);
}

void AnimationTrackEditor::_cancel_bezier_edit() {
	bezier_edit->hide();
	scroll->show();
	bezier_edit_icon->set_pressed(false);
}

void AnimationTrackEditor::_bezier_edit(int p_for_track) {
	_clear_selection(); // Bezier probably wants to use a separate selection mode.
	bezier_edit->set_root(root);
	bezier_edit->set_animation_and_track(animation, p_for_track);
	scroll->hide();
	bezier_edit->show();
	// Search everything within the track and curve - edit it.
}

void AnimationTrackEditor::_anim_duplicate_keys(bool transpose) {
	// Duplicait!
	if (selection.size() && animation.is_valid() && (!transpose || (_get_track_selected() >= 0 && _get_track_selected() < animation->get_track_count()))) {
		int top_track = 0x7FFFFFFF;
		float top_time = 1e10;
		for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
			const SelectedKey &sk = E->key();

			float t = animation->track_get_key_time(sk.track, sk.key);
			if (t < top_time) {
				top_time = t;
			}
			if (sk.track < top_track) {
				top_track = sk.track;
			}
		}
		ERR_FAIL_COND(top_track == 0x7FFFFFFF || top_time == 1e10);

		//

		int start_track = transpose ? _get_track_selected() : top_track;

		undo_redo->create_action(TTR("Anim Duplicate Keys"));

		List<Pair<int, float>> new_selection_values;

		for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
			const SelectedKey &sk = E->key();

			float t = animation->track_get_key_time(sk.track, sk.key);

			float dst_time = t + (timeline->get_play_position() - top_time);
			int dst_track = sk.track + (start_track - top_track);

			if (dst_track < 0 || dst_track >= animation->get_track_count()) {
				continue;
			}

			if (animation->track_get_type(dst_track) != animation->track_get_type(sk.track)) {
				continue;
			}

			int existing_idx = animation->track_find_key(dst_track, dst_time, true);

			undo_redo->add_do_method(animation.ptr(), "track_insert_key", dst_track, dst_time, animation->track_get_key_value(E->key().track, E->key().key), animation->track_get_key_transition(E->key().track, E->key().key));
			undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", dst_track, dst_time);

			Pair<int, float> p;
			p.first = dst_track;
			p.second = dst_time;
			new_selection_values.push_back(p);

			if (existing_idx != -1) {
				undo_redo->add_undo_method(animation.ptr(), "track_insert_key", dst_track, dst_time, animation->track_get_key_value(dst_track, existing_idx), animation->track_get_key_transition(dst_track, existing_idx));
			}
		}

		undo_redo->commit_action();

		// Reselect duplicated.

		RBMap<SelectedKey, KeyInfo> new_selection;
		for (const Pair<int, float> &E : new_selection_values) {
			int track = E.first;
			float time = E.second;

			int existing_idx = animation->track_find_key(track, time, true);

			if (existing_idx == -1) {
				continue;
			}
			SelectedKey sk2;
			sk2.track = track;
			sk2.key = existing_idx;

			KeyInfo ki;
			ki.pos = time;

			new_selection[sk2] = ki;
		}

		selection = new_selection;
		_update_tracks();
		_update_key_edit();
	}
}

void AnimationTrackEditor::_edit_menu_about_to_popup() {
	AnimationPlayer *player = AnimationPlayerEditor::get_singleton()->get_player();
	edit->get_popup()->set_item_disabled(edit->get_popup()->get_item_index(EDIT_APPLY_RESET), !player->can_apply_reset());
}

void AnimationTrackEditor::goto_prev_step(bool p_from_mouse_event) {
	if (animation.is_null()) {
		return;
	}
	float step = animation->get_step();
	if (step == 0) {
		step = 1;
	}
	if (p_from_mouse_event && Input::get_singleton()->is_key_pressed(Key::SHIFT)) {
		// Use more precise snapping when holding Shift.
		// This is used when scrobbling the timeline using Alt + Mouse wheel.
		step *= 0.25;
	}

	float pos = timeline->get_play_position();
	pos = Math::snapped(pos - step, step);
	if (pos < 0) {
		pos = 0;
	}
	set_anim_pos(pos);
	emit_signal(SNAME("timeline_changed"), pos, true, false);
}

void AnimationTrackEditor::goto_next_step(bool p_from_mouse_event) {
	if (animation.is_null()) {
		return;
	}
	float step = animation->get_step();
	if (step == 0) {
		step = 1;
	}
	if (p_from_mouse_event && Input::get_singleton()->is_key_pressed(Key::SHIFT)) {
		// Use more precise snapping when holding Shift.
		// This is used when scrobbling the timeline using Alt + Mouse wheel.
		// Do not use precise snapping when using the menu action or keyboard shortcut,
		// as the default keyboard shortcut requires pressing Shift.
		step *= 0.25;
	}

	float pos = timeline->get_play_position();

	pos = Math::snapped(pos + step, step);
	if (pos > animation->get_length()) {
		pos = animation->get_length();
	}
	set_anim_pos(pos);

	emit_signal(SNAME("timeline_changed"), pos, true, false);
}

void AnimationTrackEditor::_edit_menu_pressed(int p_option) {
	last_menu_track_opt = p_option;
	switch (p_option) {
		case EDIT_COPY_TRACKS: {
			track_copy_select->clear();
			TreeItem *troot = track_copy_select->create_item();

			for (int i = 0; i < animation->get_track_count(); i++) {
				NodePath path = animation->track_get_path(i);
				Node *node = nullptr;

				if (root && root->has_node(path)) {
					node = root->get_node(path);
				}

				String text;
				Ref<Texture2D> icon = get_theme_icon(SNAME("Node"), SNAME("EditorIcons"));
				if (node) {
					if (has_theme_icon(node->get_class(), SNAME("EditorIcons"))) {
						icon = get_theme_icon(node->get_class(), SNAME("EditorIcons"));
					}

					text = node->get_name();
					Vector<StringName> sn = path.get_subnames();
					for (int j = 0; j < sn.size(); j++) {
						text += ".";
						text += sn[j];
					}

					path = NodePath(node->get_path().get_names(), path.get_subnames(), true); // Store full path instead for copying.
				} else {
					text = path;
					int sep = text.find(":");
					if (sep != -1) {
						text = text.substr(sep + 1, text.length());
					}
				}

				String track_type;
				switch (animation->track_get_type(i)) {
					case Animation::TYPE_POSITION_3D:
						track_type = TTR("Position");
						break;
					case Animation::TYPE_ROTATION_3D:
						track_type = TTR("Rotation");
						break;
					case Animation::TYPE_SCALE_3D:
						track_type = TTR("Scale");
						break;
					case Animation::TYPE_BLEND_SHAPE:
						track_type = TTR("BlendShape");
						break;
					case Animation::TYPE_METHOD:
						track_type = TTR("Methods");
						break;
					case Animation::TYPE_BEZIER:
						track_type = TTR("Bezier");
						break;
					case Animation::TYPE_AUDIO:
						track_type = TTR("Audio");
						break;
					default: {
					};
				}
				if (!track_type.is_empty()) {
					text += vformat(" (%s)", track_type);
				}

				TreeItem *it = track_copy_select->create_item(troot);
				it->set_editable(0, true);
				it->set_selectable(0, true);
				it->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
				it->set_icon(0, icon);
				it->set_text(0, text);
				Dictionary md;
				md["track_idx"] = i;
				md["path"] = path;
				it->set_metadata(0, md);
			}

			track_copy_dialog->popup_centered(Size2(350, 500) * EDSCALE);
		} break;
		case EDIT_COPY_TRACKS_CONFIRM: {
			track_clipboard.clear();
			TreeItem *root = track_copy_select->get_root();
			if (root) {
				TreeItem *it = root->get_first_child();
				while (it) {
					Dictionary md = it->get_metadata(0);
					int idx = md["track_idx"];
					if (it->is_checked(0) && idx >= 0 && idx < animation->get_track_count()) {
						TrackClipboard tc;
						tc.base_path = animation->track_get_path(idx);
						tc.full_path = md["path"];
						tc.track_type = animation->track_get_type(idx);
						tc.interp_type = animation->track_get_interpolation_type(idx);
						if (tc.track_type == Animation::TYPE_VALUE) {
							tc.update_mode = animation->value_track_get_update_mode(idx);
						}
						tc.loop_wrap = animation->track_get_interpolation_loop_wrap(idx);
						tc.enabled = animation->track_is_enabled(idx);
						for (int i = 0; i < animation->track_get_key_count(idx); i++) {
							TrackClipboard::Key k;
							k.time = animation->track_get_key_time(idx, i);
							k.value = animation->track_get_key_value(idx, i);
							k.transition = animation->track_get_key_transition(idx, i);
							tc.keys.push_back(k);
						}
						track_clipboard.push_back(tc);
					}
					it = it->get_next();
				}
			}
		} break;
		case EDIT_PASTE_TRACKS: {
			if (track_clipboard.size() == 0) {
				EditorNode::get_singleton()->show_warning(TTR("Clipboard is empty!"));
				break;
			}

			int base_track = animation->get_track_count();
			undo_redo->create_action(TTR("Paste Tracks"));
			for (int i = 0; i < track_clipboard.size(); i++) {
				undo_redo->add_do_method(animation.ptr(), "add_track", track_clipboard[i].track_type);
				Node *exists = nullptr;
				NodePath path = track_clipboard[i].base_path;

				if (root) {
					NodePath np = track_clipboard[i].full_path;
					exists = root->get_node(np);
					if (exists) {
						path = NodePath(root->get_path_to(exists).get_names(), track_clipboard[i].full_path.get_subnames(), false);
					}
				}

				undo_redo->add_do_method(animation.ptr(), "track_set_path", base_track, path);
				undo_redo->add_do_method(animation.ptr(), "track_set_interpolation_type", base_track, track_clipboard[i].interp_type);
				undo_redo->add_do_method(animation.ptr(), "track_set_interpolation_loop_wrap", base_track, track_clipboard[i].loop_wrap);
				undo_redo->add_do_method(animation.ptr(), "track_set_enabled", base_track, track_clipboard[i].enabled);
				if (track_clipboard[i].track_type == Animation::TYPE_VALUE) {
					undo_redo->add_do_method(animation.ptr(), "value_track_set_update_mode", base_track, track_clipboard[i].update_mode);
				}

				for (int j = 0; j < track_clipboard[i].keys.size(); j++) {
					undo_redo->add_do_method(animation.ptr(), "track_insert_key", base_track, track_clipboard[i].keys[j].time, track_clipboard[i].keys[j].value, track_clipboard[i].keys[j].transition);
				}

				undo_redo->add_undo_method(animation.ptr(), "remove_track", animation->get_track_count());

				base_track++;
			}

			undo_redo->commit_action();
		} break;

		case EDIT_SCALE_SELECTION:
		case EDIT_SCALE_FROM_CURSOR: {
			scale_dialog->popup_centered(Size2(200, 100) * EDSCALE);
		} break;
		case EDIT_SCALE_CONFIRM: {
			if (selection.is_empty()) {
				return;
			}

			float from_t = 1e20;
			float to_t = -1e20;
			float len = -1e20;
			float pivot = 0;

			for (const KeyValue<SelectedKey, KeyInfo> &E : selection) {
				float t = animation->track_get_key_time(E.key.track, E.key.key);
				if (t < from_t) {
					from_t = t;
				}
				if (t > to_t) {
					to_t = t;
				}
			}

			len = to_t - from_t;
			if (last_menu_track_opt == EDIT_SCALE_FROM_CURSOR) {
				pivot = timeline->get_play_position();

			} else {
				pivot = from_t;
			}

			float s = scale->get_value();
			if (s == 0) {
				ERR_PRINT("Can't scale to 0");
			}

			undo_redo->create_action(TTR("Anim Scale Keys"));

			List<_AnimMoveRestore> to_restore;

			// 1 - Remove the keys.
			for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
				undo_redo->add_do_method(animation.ptr(), "track_remove_key", E->key().track, E->key().key);
			}
			// 2 - Remove overlapped keys.
			for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
				float newtime = (E->get().pos - from_t) * s + from_t;
				int idx = animation->track_find_key(E->key().track, newtime, true);
				if (idx == -1) {
					continue;
				}
				SelectedKey sk;
				sk.key = idx;
				sk.track = E->key().track;
				if (selection.has(sk)) {
					continue; // Already in selection, don't save.
				}

				undo_redo->add_do_method(animation.ptr(), "track_remove_key_at_time", E->key().track, newtime);
				_AnimMoveRestore amr;

				amr.key = animation->track_get_key_value(E->key().track, idx);
				amr.track = E->key().track;
				amr.time = newtime;
				amr.transition = animation->track_get_key_transition(E->key().track, idx);

				to_restore.push_back(amr);
			}

#define NEW_POS(m_ofs) (((s > 0) ? m_ofs : from_t + (len - (m_ofs - from_t))) - pivot) * ABS(s) + from_t
			// 3 - Move the keys (re insert them).
			for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
				float newpos = NEW_POS(E->get().pos);
				undo_redo->add_do_method(animation.ptr(), "track_insert_key", E->key().track, newpos, animation->track_get_key_value(E->key().track, E->key().key), animation->track_get_key_transition(E->key().track, E->key().key));
			}

			// 4 - (Undo) Remove inserted keys.
			for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
				float newpos = NEW_POS(E->get().pos);
				undo_redo->add_undo_method(animation.ptr(), "track_remove_key_at_time", E->key().track, newpos);
			}

			// 5 - (Undo) Reinsert keys.
			for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
				undo_redo->add_undo_method(animation.ptr(), "track_insert_key", E->key().track, E->get().pos, animation->track_get_key_value(E->key().track, E->key().key), animation->track_get_key_transition(E->key().track, E->key().key));
			}

			// 6 - (Undo) Reinsert overlapped keys.
			for (_AnimMoveRestore &amr : to_restore) {
				undo_redo->add_undo_method(animation.ptr(), "track_insert_key", amr.track, amr.time, amr.key, amr.transition);
			}

			undo_redo->add_do_method(this, "_clear_selection_for_anim", animation);
			undo_redo->add_undo_method(this, "_clear_selection_for_anim", animation);

			// 7-reselect.
			for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
				float oldpos = E->get().pos;
				float newpos = NEW_POS(oldpos);
				if (newpos >= 0) {
					undo_redo->add_do_method(this, "_select_at_anim", animation, E->key().track, newpos);
				}
				undo_redo->add_undo_method(this, "_select_at_anim", animation, E->key().track, oldpos);
			}
#undef NEW_POS
			undo_redo->commit_action();
		} break;
		case EDIT_DUPLICATE_SELECTION: {
			if (bezier_edit->is_visible()) {
				bezier_edit->duplicate_selection();
				break;
			}
			_anim_duplicate_keys(false);
		} break;
		case EDIT_DUPLICATE_TRANSPOSED: {
			if (bezier_edit->is_visible()) {
				EditorNode::get_singleton()->show_warning(TTR("This option does not work for Bezier editing, as it's only a single track."));
				break;
			}
			_anim_duplicate_keys(true);
		} break;
		case EDIT_ADD_RESET_KEY: {
			undo_redo->create_action(TTR("Anim Add RESET Keys"));
			Ref<Animation> reset = _create_and_get_reset_animation();
			int reset_tracks = reset->get_track_count();
			HashSet<int> tracks_added;

			for (const KeyValue<SelectedKey, KeyInfo> &E : selection) {
				const SelectedKey &sk = E.key;

				// Only add one key per track.
				if (tracks_added.has(sk.track)) {
					continue;
				}
				tracks_added.insert(sk.track);

				int dst_track = -1;

				const NodePath &path = animation->track_get_path(sk.track);
				for (int i = 0; i < reset->get_track_count(); i++) {
					if (reset->track_get_path(i) == path) {
						dst_track = i;
						break;
					}
				}

				int existing_idx = -1;
				if (dst_track == -1) {
					// If adding multiple tracks, make sure that correct track is referenced.
					dst_track = reset_tracks;
					reset_tracks++;

					undo_redo->add_do_method(reset.ptr(), "add_track", animation->track_get_type(sk.track));
					undo_redo->add_do_method(reset.ptr(), "track_set_path", dst_track, path);
					undo_redo->add_undo_method(reset.ptr(), "remove_track", dst_track);
				} else {
					existing_idx = reset->track_find_key(dst_track, 0, true);
				}

				undo_redo->add_do_method(reset.ptr(), "track_insert_key", dst_track, 0, animation->track_get_key_value(sk.track, sk.key), animation->track_get_key_transition(sk.track, sk.key));
				undo_redo->add_undo_method(reset.ptr(), "track_remove_key_at_time", dst_track, 0);

				if (existing_idx != -1) {
					undo_redo->add_undo_method(reset.ptr(), "track_insert_key", dst_track, 0, reset->track_get_key_value(dst_track, existing_idx), reset->track_get_key_transition(dst_track, existing_idx));
				}
			}

			undo_redo->commit_action();

		} break;
		case EDIT_DELETE_SELECTION: {
			if (bezier_edit->is_visible()) {
				bezier_edit->delete_selection();
				break;
			}

			if (selection.size()) {
				undo_redo->create_action(TTR("Anim Delete Keys"));

				for (RBMap<SelectedKey, KeyInfo>::Element *E = selection.back(); E; E = E->prev()) {
					undo_redo->add_do_method(animation.ptr(), "track_remove_key", E->key().track, E->key().key);
					undo_redo->add_undo_method(animation.ptr(), "track_insert_key", E->key().track, E->get().pos, animation->track_get_key_value(E->key().track, E->key().key), animation->track_get_key_transition(E->key().track, E->key().key));
				}
				undo_redo->add_do_method(this, "_clear_selection_for_anim", animation);
				undo_redo->add_undo_method(this, "_clear_selection_for_anim", animation);
				undo_redo->commit_action();
				_update_key_edit();
			}
		} break;
		case EDIT_GOTO_NEXT_STEP: {
			goto_next_step(false);
		} break;
		case EDIT_GOTO_PREV_STEP: {
			goto_prev_step(false);
		} break;
		case EDIT_APPLY_RESET: {
			AnimationPlayerEditor::get_singleton()->get_player()->apply_reset(true);

		} break;
		case EDIT_OPTIMIZE_ANIMATION: {
			optimize_dialog->popup_centered(Size2(250, 180) * EDSCALE);

		} break;
		case EDIT_OPTIMIZE_ANIMATION_CONFIRM: {
			animation->optimize(optimize_linear_error->get_value(), optimize_angular_error->get_value(), optimize_max_angle->get_value());
			_update_tracks();
			undo_redo->clear_history();

		} break;
		case EDIT_CLEAN_UP_ANIMATION: {
			cleanup_dialog->popup_centered(Size2(300, 0) * EDSCALE);

		} break;
		case EDIT_CLEAN_UP_ANIMATION_CONFIRM: {
			if (cleanup_all->is_pressed()) {
				List<StringName> names;
				AnimationPlayerEditor::get_singleton()->get_player()->get_animation_list(&names);
				for (const StringName &E : names) {
					_cleanup_animation(AnimationPlayerEditor::get_singleton()->get_player()->get_animation(E));
				}
			} else {
				_cleanup_animation(animation);
			}

		} break;
	}
}

void AnimationTrackEditor::_cleanup_animation(Ref<Animation> p_animation) {
	for (int i = 0; i < p_animation->get_track_count(); i++) {
		bool prop_exists = false;
		Variant::Type valid_type = Variant::NIL;
		Object *obj = nullptr;

		Ref<Resource> res;
		Vector<StringName> leftover_path;

		Node *node = root->get_node_and_resource(p_animation->track_get_path(i), res, leftover_path);

		if (res.is_valid()) {
			obj = res.ptr();
		} else if (node) {
			obj = node;
		}

		if (obj && p_animation->track_get_type(i) == Animation::TYPE_VALUE) {
			valid_type = obj->get_static_property_type_indexed(leftover_path, &prop_exists);
		}

		if (!obj && cleanup_tracks->is_pressed()) {
			p_animation->remove_track(i);
			i--;
			continue;
		}

		if (!prop_exists || p_animation->track_get_type(i) != Animation::TYPE_VALUE || !cleanup_keys->is_pressed()) {
			continue;
		}

		for (int j = 0; j < p_animation->track_get_key_count(i); j++) {
			Variant v = p_animation->track_get_key_value(i, j);

			if (!Variant::can_convert(v.get_type(), valid_type)) {
				p_animation->track_remove_key(i, j);
				j--;
			}
		}

		if (p_animation->track_get_key_count(i) == 0 && cleanup_tracks->is_pressed()) {
			p_animation->remove_track(i);
			i--;
		}
	}

	undo_redo->clear_history();
	_update_tracks();
}

void AnimationTrackEditor::_view_group_toggle() {
	_update_tracks();
	view_group->set_icon(get_theme_icon(view_group->is_pressed() ? SNAME("AnimationTrackList") : SNAME("AnimationTrackGroup"), SNAME("EditorIcons")));
	bezier_edit->set_filtered(selected_filter->is_pressed());
}

bool AnimationTrackEditor::is_grouping_tracks() {
	if (!view_group) {
		return false;
	}

	return !view_group->is_pressed();
}

void AnimationTrackEditor::_selection_changed() {
	if (selected_filter->is_pressed()) {
		_update_tracks(); // Needs updatin.
	} else {
		for (int i = 0; i < track_edits.size(); i++) {
			track_edits[i]->update();
		}

		for (int i = 0; i < groups.size(); i++) {
			groups[i]->update();
		}
	}
}

float AnimationTrackEditor::snap_time(float p_value, bool p_relative) {
	if (is_snap_enabled()) {
		double snap_increment;
		if (timeline->is_using_fps() && step->get_value() > 0) {
			snap_increment = 1.0 / step->get_value();
		} else {
			snap_increment = step->get_value();
		}

		if (Input::get_singleton()->is_key_pressed(Key::SHIFT)) {
			// Use more precise snapping when holding Shift.
			snap_increment *= 0.25;
		}

		if (p_relative) {
			double rel = Math::fmod(timeline->get_value(), snap_increment);
			p_value = Math::snapped(p_value + rel, snap_increment) - rel;
		} else {
			p_value = Math::snapped(p_value, snap_increment);
		}
	}

	return p_value;
}

void AnimationTrackEditor::_show_imported_anim_warning() {
	// It looks terrible on a single line but the TTR extractor doesn't support line breaks yet.
	EditorNode::get_singleton()->show_warning(TTR("This animation belongs to an imported scene, so changes to imported tracks will not be saved.\n\nTo enable the ability to add custom tracks, navigate to the scene's import settings and set\n\"Animation > Storage\" to \"Files\", enable \"Animation > Keep Custom Tracks\", then re-import.\nAlternatively, use an import preset that imports animations to separate files."),
			TTR("Warning: Editing imported animation"));
}

void AnimationTrackEditor::_select_all_tracks_for_copy() {
	TreeItem *track = track_copy_select->get_root()->get_first_child();
	if (!track) {
		return;
	}

	bool all_selected = true;
	while (track) {
		if (!track->is_checked(0)) {
			all_selected = false;
		}

		track = track->get_next();
	}

	track = track_copy_select->get_root()->get_first_child();
	while (track) {
		track->set_checked(0, !all_selected);
		track = track->get_next();
	}
}

void AnimationTrackEditor::_bind_methods() {
	ClassDB::bind_method("_animation_update", &AnimationTrackEditor::_animation_update);
	ClassDB::bind_method("_track_grab_focus", &AnimationTrackEditor::_track_grab_focus);
	ClassDB::bind_method("_update_tracks", &AnimationTrackEditor::_update_tracks);
	ClassDB::bind_method("_clear_selection_for_anim", &AnimationTrackEditor::_clear_selection_for_anim);
	ClassDB::bind_method("_select_at_anim", &AnimationTrackEditor::_select_at_anim);

	ClassDB::bind_method("_key_selected", &AnimationTrackEditor::_key_selected); // Still used by some connect_compat.
	ClassDB::bind_method("_key_deselected", &AnimationTrackEditor::_key_deselected); // Still used by some connect_compat.
	ClassDB::bind_method("_clear_selection", &AnimationTrackEditor::_clear_selection); // Still used by some connect_compat.

	ADD_SIGNAL(MethodInfo("timeline_changed", PropertyInfo(Variant::FLOAT, "position"), PropertyInfo(Variant::BOOL, "drag"), PropertyInfo(Variant::BOOL, "timeline_only")));
	ADD_SIGNAL(MethodInfo("keying_changed"));
	ADD_SIGNAL(MethodInfo("animation_len_changed", PropertyInfo(Variant::FLOAT, "len")));
	ADD_SIGNAL(MethodInfo("animation_step_changed", PropertyInfo(Variant::FLOAT, "step")));
}

void AnimationTrackEditor::_pick_track_filter_text_changed(const String &p_newtext) {
	TreeItem *root_item = pick_track->get_scene_tree()->get_scene_tree()->get_root();

	Vector<Node *> select_candidates;
	Node *to_select = nullptr;

	String filter = pick_track->get_filter_line_edit()->get_text();

	_pick_track_select_recursive(root_item, filter, select_candidates);

	if (!select_candidates.is_empty()) {
		for (int i = 0; i < select_candidates.size(); ++i) {
			Node *candidate = select_candidates[i];

			if (((String)candidate->get_name()).to_lower().begins_with(filter.to_lower())) {
				to_select = candidate;
				break;
			}
		}

		if (!to_select) {
			to_select = select_candidates[0];
		}
	}

	pick_track->get_scene_tree()->set_selected(to_select);
}

void AnimationTrackEditor::_pick_track_select_recursive(TreeItem *p_item, const String &p_filter, Vector<Node *> &p_select_candidates) {
	if (!p_item) {
		return;
	}

	NodePath np = p_item->get_metadata(0);
	Node *node = get_node(np);

	if (!p_filter.is_empty() && ((String)node->get_name()).findn(p_filter) != -1) {
		p_select_candidates.push_back(node);
	}

	TreeItem *c = p_item->get_first_child();

	while (c) {
		_pick_track_select_recursive(c, p_filter, p_select_candidates);
		c = c->get_next();
	}
}

void AnimationTrackEditor::_pick_track_filter_input(const Ref<InputEvent> &p_ie) {
	Ref<InputEventKey> k = p_ie;

	if (k.is_valid()) {
		switch (k->get_keycode()) {
			case Key::UP:
			case Key::DOWN:
			case Key::PAGEUP:
			case Key::PAGEDOWN: {
				pick_track->get_scene_tree()->get_scene_tree()->gui_input(k);
				pick_track->get_filter_line_edit()->accept_event();
			} break;
			default:
				break;
		}
	}
}

AnimationTrackEditor::AnimationTrackEditor() {
	undo_redo = EditorNode::get_singleton()->get_undo_redo();

	main_panel = memnew(PanelContainer);
	main_panel->set_focus_mode(FOCUS_ALL); // Allow panel to have focus so that shortcuts work as expected.
	add_child(main_panel);
	main_panel->set_v_size_flags(SIZE_EXPAND_FILL);
	HBoxContainer *timeline_scroll = memnew(HBoxContainer);
	main_panel->add_child(timeline_scroll);
	timeline_scroll->set_v_size_flags(SIZE_EXPAND_FILL);

	VBoxContainer *timeline_vbox = memnew(VBoxContainer);
	timeline_scroll->add_child(timeline_vbox);
	timeline_vbox->set_v_size_flags(SIZE_EXPAND_FILL);
	timeline_vbox->set_h_size_flags(SIZE_EXPAND_FILL);
	timeline_vbox->add_theme_constant_override("separation", 0);

	info_message = memnew(Label);
	info_message->set_text(TTR("Select an AnimationPlayer node to create and edit animations."));
	info_message->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	info_message->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	info_message->set_autowrap_mode(Label::AUTOWRAP_WORD_SMART);
	info_message->set_custom_minimum_size(Size2(100 * EDSCALE, 0));
	info_message->set_anchors_and_offsets_preset(PRESET_WIDE, PRESET_MODE_KEEP_SIZE, 8 * EDSCALE);
	main_panel->add_child(info_message);

	timeline = memnew(AnimationTimelineEdit);
	timeline->set_undo_redo(undo_redo);
	timeline_vbox->add_child(timeline);
	timeline->connect("timeline_changed", callable_mp(this, &AnimationTrackEditor::_timeline_changed));
	timeline->connect("name_limit_changed", callable_mp(this, &AnimationTrackEditor::_name_limit_changed));
	timeline->connect("track_added", callable_mp(this, &AnimationTrackEditor::_add_track));
	timeline->connect("value_changed", callable_mp(this, &AnimationTrackEditor::_timeline_value_changed));
	timeline->connect("length_changed", callable_mp(this, &AnimationTrackEditor::_update_length));

	panner.instantiate();
	panner->set_callbacks(callable_mp(this, &AnimationTrackEditor::_scroll_callback), callable_mp(this, &AnimationTrackEditor::_pan_callback), callable_mp(this, &AnimationTrackEditor::_zoom_callback));

	scroll = memnew(ScrollContainer);
	timeline_vbox->add_child(scroll);
	scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	VScrollBar *sb = scroll->get_v_scroll_bar();
	scroll->remove_child(sb);
	timeline_scroll->add_child(sb); // Move here so timeline and tracks are always aligned.
	scroll->set_focus_mode(FOCUS_CLICK);
	scroll->connect("gui_input", callable_mp(this, &AnimationTrackEditor::_scroll_input));
	scroll->connect("focus_exited", callable_mp(panner.ptr(), &ViewPanner::release_pan_key));

	bezier_edit = memnew(AnimationBezierTrackEdit);
	timeline_vbox->add_child(bezier_edit);
	bezier_edit->set_undo_redo(undo_redo);
	bezier_edit->set_editor(this);
	bezier_edit->set_timeline(timeline);
	bezier_edit->hide();
	bezier_edit->set_v_size_flags(SIZE_EXPAND_FILL);
	bezier_edit->connect("close_request", callable_mp(this, &AnimationTrackEditor::_cancel_bezier_edit));

	timeline_vbox->set_custom_minimum_size(Size2(0, 150) * EDSCALE);

	hscroll = memnew(HScrollBar);
	hscroll->share(timeline);
	hscroll->hide();
	hscroll->connect("value_changed", callable_mp(this, &AnimationTrackEditor::_update_scroll));
	timeline_vbox->add_child(hscroll);
	timeline->set_hscroll(hscroll);

	track_vbox = memnew(VBoxContainer);
	scroll->add_child(track_vbox);
	track_vbox->set_h_size_flags(SIZE_EXPAND_FILL);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	track_vbox->add_theme_constant_override("separation", 0);

	HBoxContainer *bottom_hb = memnew(HBoxContainer);
	add_child(bottom_hb);

	imported_anim_warning = memnew(Button);
	imported_anim_warning->hide();
	imported_anim_warning->set_tooltip(TTR("Warning: Editing imported animation"));
	imported_anim_warning->connect("pressed", callable_mp(this, &AnimationTrackEditor::_show_imported_anim_warning));
	bottom_hb->add_child(imported_anim_warning);

	bottom_hb->add_spacer();

	bezier_edit_icon = memnew(Button);
	bezier_edit_icon->set_flat(true);
	bezier_edit_icon->set_disabled(true);
	bezier_edit_icon->set_toggle_mode(true);
	bezier_edit_icon->connect("pressed", callable_mp(this, &AnimationTrackEditor::_toggle_bezier_edit));
	bezier_edit_icon->set_tooltip(TTR("Toggle between the bezier curve editor and track editor."));

	bottom_hb->add_child(bezier_edit_icon);

	selected_filter = memnew(Button);
	selected_filter->set_flat(true);
	selected_filter->connect("pressed", callable_mp(this, &AnimationTrackEditor::_view_group_toggle)); // Same function works the same.
	selected_filter->set_toggle_mode(true);
	selected_filter->set_tooltip(TTR("Only show tracks from nodes selected in tree."));

	bottom_hb->add_child(selected_filter);

	view_group = memnew(Button);
	view_group->set_flat(true);
	view_group->connect("pressed", callable_mp(this, &AnimationTrackEditor::_view_group_toggle));
	view_group->set_toggle_mode(true);
	view_group->set_tooltip(TTR("Group tracks by node or display them as plain list."));

	bottom_hb->add_child(view_group);
	bottom_hb->add_child(memnew(VSeparator));

	snap = memnew(Button);
	snap->set_flat(true);
	snap->set_text(TTR("Snap:") + " ");
	bottom_hb->add_child(snap);
	snap->set_disabled(true);
	snap->set_toggle_mode(true);
	snap->set_pressed(true);

	step = memnew(EditorSpinSlider);
	step->set_min(0);
	step->set_max(1000000);
	step->set_step(0.001);
	step->set_hide_slider(true);
	step->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	step->set_tooltip(TTR("Animation step value."));
	bottom_hb->add_child(step);
	step->connect("value_changed", callable_mp(this, &AnimationTrackEditor::_update_step));
	step->set_read_only(true);

	snap_mode = memnew(OptionButton);
	snap_mode->add_item(TTR("Seconds"));
	snap_mode->add_item(TTR("FPS"));
	bottom_hb->add_child(snap_mode);
	snap_mode->connect("item_selected", callable_mp(this, &AnimationTrackEditor::_snap_mode_changed));
	snap_mode->set_disabled(true);

	bottom_hb->add_child(memnew(VSeparator));

	zoom_icon = memnew(TextureRect);
	zoom_icon->set_v_size_flags(SIZE_SHRINK_CENTER);
	bottom_hb->add_child(zoom_icon);
	zoom = memnew(HSlider);
	zoom->set_step(0.01);
	zoom->set_min(0.0);
	zoom->set_max(2.0);
	zoom->set_value(1.0);
	zoom->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	zoom->set_v_size_flags(SIZE_SHRINK_CENTER);
	bottom_hb->add_child(zoom);
	timeline->set_zoom(zoom);

	edit = memnew(MenuButton);
	edit->set_shortcut_context(this);
	edit->set_text(TTR("Edit"));
	edit->set_flat(false);
	edit->set_disabled(true);
	edit->set_tooltip(TTR("Animation properties."));
	edit->get_popup()->add_item(TTR("Copy Tracks"), EDIT_COPY_TRACKS);
	edit->get_popup()->add_item(TTR("Paste Tracks"), EDIT_PASTE_TRACKS);
	edit->get_popup()->add_separator();
	edit->get_popup()->add_item(TTR("Scale Selection"), EDIT_SCALE_SELECTION);
	edit->get_popup()->add_item(TTR("Scale From Cursor"), EDIT_SCALE_FROM_CURSOR);
	edit->get_popup()->add_separator();
	edit->get_popup()->add_shortcut(ED_SHORTCUT("animation_editor/duplicate_selection", TTR("Duplicate Selection"), KeyModifierMask::CMD | Key::D), EDIT_DUPLICATE_SELECTION);
	edit->get_popup()->add_shortcut(ED_SHORTCUT("animation_editor/duplicate_selection_transposed", TTR("Duplicate Transposed"), KeyModifierMask::SHIFT | KeyModifierMask::CMD | Key::D), EDIT_DUPLICATE_TRANSPOSED);
	edit->get_popup()->add_shortcut(ED_SHORTCUT("animation_editor/add_reset_value", TTR("Add RESET Value(s)")));
	edit->get_popup()->add_separator();
	edit->get_popup()->add_shortcut(ED_SHORTCUT("animation_editor/delete_selection", TTR("Delete Selection"), Key::KEY_DELETE), EDIT_DELETE_SELECTION);

	edit->get_popup()->add_separator();
	edit->get_popup()->add_shortcut(ED_SHORTCUT("animation_editor/goto_next_step", TTR("Go to Next Step"), KeyModifierMask::CMD | Key::RIGHT), EDIT_GOTO_NEXT_STEP);
	edit->get_popup()->add_shortcut(ED_SHORTCUT("animation_editor/goto_prev_step", TTR("Go to Previous Step"), KeyModifierMask::CMD | Key::LEFT), EDIT_GOTO_PREV_STEP);
	edit->get_popup()->add_separator();
	edit->get_popup()->add_shortcut(ED_SHORTCUT("animation_editor/apply_reset", TTR("Apply Reset")), EDIT_APPLY_RESET);
	edit->get_popup()->add_separator();
	edit->get_popup()->add_item(TTR("Optimize Animation"), EDIT_OPTIMIZE_ANIMATION);
	edit->get_popup()->add_item(TTR("Clean-Up Animation"), EDIT_CLEAN_UP_ANIMATION);

	edit->get_popup()->connect("id_pressed", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed));
	edit->get_popup()->connect("about_to_popup", callable_mp(this, &AnimationTrackEditor::_edit_menu_about_to_popup));

	pick_track = memnew(SceneTreeDialog);
	add_child(pick_track);
	pick_track->register_text_enter(pick_track->get_filter_line_edit());
	pick_track->set_title(TTR("Pick a node to animate:"));
	pick_track->connect("selected", callable_mp(this, &AnimationTrackEditor::_new_track_node_selected));
	pick_track->get_filter_line_edit()->connect("text_changed", callable_mp(this, &AnimationTrackEditor::_pick_track_filter_text_changed));
	pick_track->get_filter_line_edit()->connect("gui_input", callable_mp(this, &AnimationTrackEditor::_pick_track_filter_input));

	prop_selector = memnew(PropertySelector);
	add_child(prop_selector);
	prop_selector->connect("selected", callable_mp(this, &AnimationTrackEditor::_new_track_property_selected));

	method_selector = memnew(PropertySelector);
	add_child(method_selector);
	method_selector->connect("selected", callable_mp(this, &AnimationTrackEditor::_add_method_key));

	insert_confirm = memnew(ConfirmationDialog);
	add_child(insert_confirm);
	insert_confirm->connect("confirmed", callable_mp(this, &AnimationTrackEditor::_confirm_insert_list));
	VBoxContainer *icvb = memnew(VBoxContainer);
	insert_confirm->add_child(icvb);
	insert_confirm_text = memnew(Label);
	icvb->add_child(insert_confirm_text);
	HBoxContainer *ichb = memnew(HBoxContainer);
	icvb->add_child(ichb);
	insert_confirm_bezier = memnew(CheckBox);
	insert_confirm_bezier->set_text(TTR("Use Bezier Curves"));
	insert_confirm_bezier->set_pressed(EDITOR_GET("editors/animation/default_create_bezier_tracks"));
	ichb->add_child(insert_confirm_bezier);
	insert_confirm_reset = memnew(CheckBox);
	insert_confirm_reset->set_text(TTR("Create RESET Track(s)", ""));
	insert_confirm_reset->set_pressed(EDITOR_GET("editors/animation/default_create_reset_tracks"));
	ichb->add_child(insert_confirm_reset);

	box_selection = memnew(Control);
	add_child(box_selection);
	box_selection->set_as_top_level(true);
	box_selection->set_mouse_filter(MOUSE_FILTER_IGNORE);
	box_selection->hide();
	box_selection->connect("draw", callable_mp(this, &AnimationTrackEditor::_box_selection_draw));

	// Default Plugins.

	Ref<AnimationTrackEditDefaultPlugin> def_plugin;
	def_plugin.instantiate();
	add_track_edit_plugin(def_plugin);

	// Dialogs.

	optimize_dialog = memnew(ConfirmationDialog);
	add_child(optimize_dialog);
	optimize_dialog->set_title(TTR("Anim. Optimizer"));
	VBoxContainer *optimize_vb = memnew(VBoxContainer);
	optimize_dialog->add_child(optimize_vb);

	optimize_linear_error = memnew(SpinBox);
	optimize_linear_error->set_max(1.0);
	optimize_linear_error->set_min(0.001);
	optimize_linear_error->set_step(0.001);
	optimize_linear_error->set_value(0.05);
	optimize_vb->add_margin_child(TTR("Max. Linear Error:"), optimize_linear_error);
	optimize_angular_error = memnew(SpinBox);
	optimize_angular_error->set_max(1.0);
	optimize_angular_error->set_min(0.001);
	optimize_angular_error->set_step(0.001);
	optimize_angular_error->set_value(0.01);

	optimize_vb->add_margin_child(TTR("Max. Angular Error:"), optimize_angular_error);
	optimize_max_angle = memnew(SpinBox);
	optimize_vb->add_margin_child(TTR("Max Optimizable Angle:"), optimize_max_angle);
	optimize_max_angle->set_max(360.0);
	optimize_max_angle->set_min(0.0);
	optimize_max_angle->set_step(0.1);
	optimize_max_angle->set_value(22);

	optimize_dialog->get_ok_button()->set_text(TTR("Optimize"));
	optimize_dialog->connect("confirmed", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed), varray(EDIT_OPTIMIZE_ANIMATION_CONFIRM));

	//

	cleanup_dialog = memnew(ConfirmationDialog);
	add_child(cleanup_dialog);
	VBoxContainer *cleanup_vb = memnew(VBoxContainer);
	cleanup_dialog->add_child(cleanup_vb);

	cleanup_keys = memnew(CheckBox);
	cleanup_keys->set_text(TTR("Remove invalid keys"));
	cleanup_keys->set_pressed(true);
	cleanup_vb->add_child(cleanup_keys);

	cleanup_tracks = memnew(CheckBox);
	cleanup_tracks->set_text(TTR("Remove unresolved and empty tracks"));
	cleanup_tracks->set_pressed(true);
	cleanup_vb->add_child(cleanup_tracks);

	cleanup_all = memnew(CheckBox);
	cleanup_all->set_text(TTR("Clean-up all animations"));
	cleanup_vb->add_child(cleanup_all);

	cleanup_dialog->set_title(TTR("Clean-Up Animation(s) (NO UNDO!)"));
	cleanup_dialog->get_ok_button()->set_text(TTR("Clean-Up"));

	cleanup_dialog->connect("confirmed", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed), varray(EDIT_CLEAN_UP_ANIMATION_CONFIRM));

	//
	scale_dialog = memnew(ConfirmationDialog);
	VBoxContainer *vbc = memnew(VBoxContainer);
	scale_dialog->add_child(vbc);

	scale = memnew(SpinBox);
	scale->set_min(-99999);
	scale->set_max(99999);
	scale->set_step(0.001);
	vbc->add_margin_child(TTR("Scale Ratio:"), scale);
	scale_dialog->connect("confirmed", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed), varray(EDIT_SCALE_CONFIRM));
	add_child(scale_dialog);

	track_copy_dialog = memnew(ConfirmationDialog);
	add_child(track_copy_dialog);
	track_copy_dialog->set_title(TTR("Select Tracks to Copy"));
	track_copy_dialog->get_ok_button()->set_text(TTR("Copy"));

	VBoxContainer *track_vbox = memnew(VBoxContainer);
	track_copy_dialog->add_child(track_vbox);

	Button *select_all_button = memnew(Button);
	select_all_button->set_text(TTR("Select All/None"));
	select_all_button->connect("pressed", callable_mp(this, &AnimationTrackEditor::_select_all_tracks_for_copy));
	track_vbox->add_child(select_all_button);

	track_copy_select = memnew(Tree);
	track_copy_select->set_h_size_flags(SIZE_EXPAND_FILL);
	track_copy_select->set_v_size_flags(SIZE_EXPAND_FILL);
	track_copy_select->set_hide_root(true);
	track_vbox->add_child(track_copy_select);
	track_copy_dialog->connect("confirmed", callable_mp(this, &AnimationTrackEditor::_edit_menu_pressed), varray(EDIT_COPY_TRACKS_CONFIRM));
}

AnimationTrackEditor::~AnimationTrackEditor() {
	if (key_edit) {
		memdelete(key_edit);
	}
	if (multi_key_edit) {
		memdelete(multi_key_edit);
	}
}
