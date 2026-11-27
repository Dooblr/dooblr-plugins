{
  "patcher": {
    "fileversion": 1,
    "appversion": {
      "major": 8,
      "minor": 5,
      "revision": 8,
      "architecture": "x64",
      "modernui": 1
    },
    "classnamespace": "box",
    "rect": [
      80,
      80,
      1480,
      720
    ],
    "openrect": [
      0,
      0,
      0,
      149
    ],
    "devicewidth": 360,
    "openinpresentation": 1,
    "default_fontname": "Arial",
    "default_fontsize": 10,
    "default_fontface": 0,
    "gridsize": [
      8,
      8
    ],
    "style": "",
    "description": "Snaps incoming MIDI notes to the harmony broadcast by a dooblr-chords sender on the same bus channel.",
    "digest": "",
    "tags": "",
    "is_mpe": 0,
    "latency": 0,
    "minimum_live_version": "",
    "minimum_max_version": "",
    "platform_compatibility": 0,
    "autosave": 0,
    "bglocked": 0,
    "boxanimatetime": 200,
    "assistshowspatchername": 0,
    "enablehscroll": 1,
    "enablevscroll": 1,
    "gridonopen": 1,
    "gridsnaponopen": 1,
    "objectsnaponopen": 1,
    "statusbarvisible": 2,
    "toolbarvisible": 1,
    "lefttoolbarpinned": 0,
    "toptoolbarpinned": 0,
    "righttoolbarpinned": 0,
    "bottomtoolbarpinned": 0,
    "toolbars_unpinned_last_save": 0,
    "tallnewobj": 0,
    "subpatcher_template": "",
    "dependency_cache": [],
    "boxes": [
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "dooblr-chord-receiver",
          "fontname": "Arial Bold",
          "fontsize": 12,
          "textcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            8,
            4,
            220,
            16
          ],
          "patching_rect": [
            8,
            204,
            220,
            16
          ],
          "id": "obj-1"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "v0.1.1",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            230,
            6,
            50,
            14
          ],
          "patching_rect": [
            230,
            206,
            50,
            14
          ],
          "id": "obj-2"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "CHANNEL",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            8,
            24,
            70,
            12
          ],
          "patching_rect": [
            8,
            224,
            70,
            12
          ],
          "varname": "channel_label",
          "id": "obj-3"
        }
      },
      {
        "box": {
          "maxclass": "live.tab",
          "varname": "channel",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
          "presentation": 1,
          "presentation_rect": [
            8,
            36,
            168,
            22
          ],
          "patching_rect": [
            44,
            400,
            84,
            16
          ],
          "annotation_name": "Channel",
          "annotation": "Bus channel. Receivers only listen to a dooblr-chords sender on the same channel (A\u2013D).",
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Channel",
              "parameter_shortname": "Channel",
              "parameter_type": 2,
              "parameter_invisible": 0,
              "parameter_enum": [
                "A",
                "B",
                "C",
                "D"
              ],
              "parameter_mmax": 3,
              "parameter_initial": [
                0
              ],
              "parameter_initial_enable": 1
            }
          },
          "fontname": "Arial Bold",
          "fontsize": 10,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activebgoncolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "activetextcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activetextoncolor": [
            0.086,
            0.094,
            0.11,
            1
          ],
          "bgcolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "bgoncolor": [
            0.373,
            0.702,
            0.631,
            0.38
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            0.38
          ],
          "textoncolor": [
            0.086,
            0.094,
            0.11,
            0.38
          ],
          "bordercolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "focusbordercolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "id": "obj-4"
        }
      },
      {
        "box": {
          "maxclass": "jsui",
          "filename": "dooblr_chord_receiver_ui.js",
          "varname": "status",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "parameter_enable": 0,
          "border": 0,
          "annotation_name": "Status",
          "annotation": "Shows the connected sender and the current chord name, or NO SENDER if none is heard.",
          "presentation": 1,
          "presentation_rect": [
            184,
            24,
            168,
            34
          ],
          "patching_rect": [
            184,
            24,
            168,
            34
          ],
          "id": "obj-5"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "ROLE",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            8,
            62,
            40,
            12
          ],
          "patching_rect": [
            8,
            262,
            40,
            12
          ],
          "varname": "role_label",
          "id": "obj-6"
        }
      },
      {
        "box": {
          "maxclass": "live.tab",
          "varname": "role",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
          "presentation": 1,
          "presentation_rect": [
            50,
            60,
            302,
            22
          ],
          "patching_rect": [
            68,
            400,
            151,
            16
          ],
          "annotation_name": "Role",
          "annotation": "TONES snaps to the nearest chord tone. SCALE snaps to the scale. BASS maps to the advertised bass pitch class in the input octave. FOLLOW transposes a riff from the Reference root up to the current chord root.",
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Role",
              "parameter_shortname": "Role",
              "parameter_type": 2,
              "parameter_invisible": 0,
              "parameter_enum": [
                "TONES",
                "SCALE",
                "BASS",
                "FOLLOW"
              ],
              "parameter_mmax": 3,
              "parameter_initial": [
                0
              ],
              "parameter_initial_enable": 1
            }
          },
          "fontname": "Arial Bold",
          "fontsize": 10,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activebgoncolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "activetextcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activetextoncolor": [
            0.086,
            0.094,
            0.11,
            1
          ],
          "bgcolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "bgoncolor": [
            0.373,
            0.702,
            0.631,
            0.38
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            0.38
          ],
          "textoncolor": [
            0.086,
            0.094,
            0.11,
            0.38
          ],
          "bordercolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "focusbordercolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "id": "obj-7"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "SNAP",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            8,
            88,
            40,
            12
          ],
          "patching_rect": [
            8,
            288,
            40,
            12
          ],
          "varname": "snap_label",
          "id": "obj-8"
        }
      },
      {
        "box": {
          "maxclass": "live.tab",
          "varname": "snapdir",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
          "presentation": 1,
          "presentation_rect": [
            50,
            86,
            168,
            22
          ],
          "patching_rect": [
            84,
            400,
            98,
            16
          ],
          "annotation_name": "Snap",
          "annotation": "When a note is between two targets, snap nearest (ties go down), always up, or always down. The original octave is kept unless Range is on.",
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Snap",
              "parameter_shortname": "Snap",
              "parameter_type": 2,
              "parameter_invisible": 0,
              "parameter_enum": [
                "NEAREST",
                "UP",
                "DOWN"
              ],
              "parameter_mmax": 2,
              "parameter_initial": [
                0
              ],
              "parameter_initial_enable": 1
            }
          },
          "fontname": "Arial Bold",
          "fontsize": 10,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activebgoncolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "activetextcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activetextoncolor": [
            0.086,
            0.094,
            0.11,
            1
          ],
          "bgcolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "bgoncolor": [
            0.373,
            0.702,
            0.631,
            0.38
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            0.38
          ],
          "textoncolor": [
            0.086,
            0.094,
            0.11,
            0.38
          ],
          "bordercolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "focusbordercolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "id": "obj-9"
        }
      },
      {
        "box": {
          "maxclass": "live.tab",
          "varname": "retrigger",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
          "presentation": 1,
          "presentation_rect": [
            268,
            86,
            84,
            22
          ],
          "patching_rect": [
            92,
            400,
            49,
            16
          ],
          "annotation_name": "Retrigger",
          "annotation": "When On, held notes are re-snapped (old pitch off, new pitch on) whenever the chord or a mapping setting changes.",
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Retrigger",
              "parameter_shortname": "Retrig",
              "parameter_type": 2,
              "parameter_invisible": 0,
              "parameter_enum": [
                "OFF",
                "ON"
              ],
              "parameter_mmax": 1,
              "parameter_initial": [
                1
              ],
              "parameter_initial_enable": 1
            }
          },
          "fontname": "Arial Bold",
          "fontsize": 10,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activebgoncolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "activetextcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activetextoncolor": [
            0.086,
            0.094,
            0.11,
            1
          ],
          "bgcolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "bgoncolor": [
            0.373,
            0.702,
            0.631,
            0.38
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            0.38
          ],
          "textoncolor": [
            0.086,
            0.094,
            0.11,
            0.38
          ],
          "bordercolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "focusbordercolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "id": "obj-10"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "REF",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            8,
            114,
            28,
            12
          ],
          "patching_rect": [
            8,
            314,
            28,
            12
          ],
          "varname": "ref_label",
          "id": "obj-11"
        }
      },
      {
        "box": {
          "maxclass": "live.menu",
          "varname": "refroot",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
          "presentation": 1,
          "presentation_rect": [
            38,
            112,
            56,
            22
          ],
          "patching_rect": [
            108,
            430,
            40,
            16
          ],
          "annotation_name": "Reference",
          "annotation": "Reference root for FOLLOW: the riff is transposed 0\u201311 semitones upward from this pitch class to the current chord root.",
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Reference",
              "parameter_shortname": "Ref",
              "parameter_type": 2,
              "parameter_invisible": 0,
              "parameter_enum": [
                "C",
                "C#",
                "D",
                "Eb",
                "E",
                "F",
                "F#",
                "G",
                "Ab",
                "A",
                "Bb",
                "B"
              ],
              "parameter_mmax": 11,
              "parameter_initial": [
                0
              ],
              "parameter_initial_enable": 1
            }
          },
          "fontname": "Arial",
          "fontsize": 11,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "bgcolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activetextcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "tricolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "activetricolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "bordercolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "id": "obj-12"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "RANGE",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            100,
            114,
            44,
            12
          ],
          "patching_rect": [
            100,
            314,
            44,
            12
          ],
          "varname": "range_label",
          "id": "obj-13"
        }
      },
      {
        "box": {
          "maxclass": "live.tab",
          "varname": "range",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
          "presentation": 1,
          "presentation_rect": [
            146,
            112,
            80,
            22
          ],
          "patching_rect": [
            124,
            400,
            40,
            16
          ],
          "annotation_name": "Range",
          "annotation": "When On, snapped pitches wrap into one octave starting at Register.",
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Range",
              "parameter_shortname": "Range",
              "parameter_type": 2,
              "parameter_invisible": 0,
              "parameter_enum": [
                "OFF",
                "ON"
              ],
              "parameter_mmax": 1,
              "parameter_initial": [
                0
              ],
              "parameter_initial_enable": 1
            }
          },
          "fontname": "Arial Bold",
          "fontsize": 10,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activebgoncolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "activetextcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activetextoncolor": [
            0.086,
            0.094,
            0.11,
            1
          ],
          "bgcolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "bgoncolor": [
            0.373,
            0.702,
            0.631,
            0.38
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            0.38
          ],
          "textoncolor": [
            0.086,
            0.094,
            0.11,
            0.38
          ],
          "bordercolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "focusbordercolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "id": "obj-14"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "REG",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            232,
            114,
            28,
            12
          ],
          "patching_rect": [
            232,
            314,
            28,
            12
          ],
          "varname": "reg_label",
          "id": "obj-15"
        }
      },
      {
        "box": {
          "maxclass": "live.numbox",
          "varname": "register",
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "",
            "float"
          ],
          "parameter_enable": 1,
          "presentation": 1,
          "presentation_rect": [
            262,
            112,
            90,
            22
          ],
          "patching_rect": [
            140,
            460,
            45,
            16
          ],
          "annotation_name": "Register",
          "annotation": "Bottom of the one-octave window used when Range is On. MIDI note, displayed as a pitch name.",
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Register",
              "parameter_shortname": "Register",
              "parameter_type": 1,
              "parameter_invisible": 0,
              "parameter_mmin": 24,
              "parameter_mmax": 84,
              "parameter_unitstyle": 8,
              "parameter_initial": [
                48
              ],
              "parameter_initial_enable": 1
            }
          },
          "fontname": "Arial",
          "fontsize": 11,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "tricolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "activetricolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "activeslidercolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "bordercolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "id": "obj-16"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "live.thisdevice",
          "numinlets": 1,
          "numoutlets": 3,
          "patching_rect": [
            800,
            20,
            100,
            22
          ],
          "presentation": 0,
          "outlettype": [
            "bang",
            "int",
            "int"
          ],
          "id": "obj-17"
        }
      },
      {
        "box": {
          "maxclass": "message",
          "text": "init",
          "numinlets": 2,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "patching_rect": [
            800,
            52,
            34,
            22
          ],
          "presentation": 0,
          "id": "obj-18"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "js dooblr_chord_receiver.js",
          "numinlets": 1,
          "numoutlets": 3,
          "patching_rect": [
            800,
            200,
            220,
            22
          ],
          "presentation": 0,
          "outlettype": [
            "",
            "",
            ""
          ],
          "id": "obj-19"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "midiin",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            1100,
            20,
            50,
            22
          ],
          "presentation": 0,
          "outlettype": [
            "int"
          ],
          "id": "obj-20"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "midiout",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            1100,
            260,
            55,
            22
          ],
          "presentation": 0,
          "id": "obj-21"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "midiparse",
          "numinlets": 1,
          "numoutlets": 7,
          "patching_rect": [
            1100,
            60,
            80,
            22
          ],
          "presentation": 0,
          "outlettype": [
            "",
            "",
            "",
            "",
            "",
            "",
            ""
          ],
          "id": "obj-22"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "midiformat",
          "numinlets": 7,
          "numoutlets": 1,
          "patching_rect": [
            1260,
            160,
            90,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-23"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "send dooblr_chords_bus",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            800,
            280,
            170,
            22
          ],
          "presentation": 0,
          "id": "obj-24"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "receive dooblr_chords_bus",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            800,
            320,
            180,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-25"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend bus",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            800,
            352,
            90,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-26"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend note",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            1100,
            100,
            90,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-27"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "dooblr-chord-receiver v0.1.1 \u2014 hidden patching objects. Presentation is the device UI.",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 0,
          "patching_rect": [
            800,
            0,
            420,
            20
          ],
          "id": "obj-28"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param channel",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            20,
            520,
            140,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-29"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param role",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            20,
            544,
            140,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-30"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param snapdir",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            20,
            568,
            140,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-31"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param retrigger",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            20,
            592,
            140,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-32"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param refroot",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            20,
            616,
            140,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-33"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param range",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            20,
            640,
            140,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-34"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param register",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            20,
            664,
            140,
            22
          ],
          "presentation": 0,
          "outlettype": [
            ""
          ],
          "id": "obj-35"
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "varname": "retrigger_label",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "RETRIG",
          "fontname": "Arial",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "presentation": 1,
          "presentation_rect": [
            222,
            88,
            44,
            12
          ],
          "patching_rect": [
            222,
            288,
            44,
            12
          ],
          "id": "obj-36"
        }
      }
    ],
    "lines": [
      {
        "patchline": {
          "source": [
            "obj-17",
            0
          ],
          "destination": [
            "obj-18",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-18",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-20",
            0
          ],
          "destination": [
            "obj-22",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-22",
            0
          ],
          "destination": [
            "obj-27",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-27",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-22",
            1
          ],
          "destination": [
            "obj-23",
            1
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-22",
            2
          ],
          "destination": [
            "obj-23",
            2
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-22",
            3
          ],
          "destination": [
            "obj-23",
            3
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-22",
            4
          ],
          "destination": [
            "obj-23",
            4
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-22",
            5
          ],
          "destination": [
            "obj-23",
            5
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-23",
            0
          ],
          "destination": [
            "obj-21",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-19",
            0
          ],
          "destination": [
            "obj-21",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-19",
            1
          ],
          "destination": [
            "obj-24",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-19",
            2
          ],
          "destination": [
            "obj-5",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-25",
            0
          ],
          "destination": [
            "obj-26",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-26",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-4",
            0
          ],
          "destination": [
            "obj-29",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-29",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-7",
            0
          ],
          "destination": [
            "obj-30",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-30",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-9",
            0
          ],
          "destination": [
            "obj-31",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-31",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-10",
            0
          ],
          "destination": [
            "obj-32",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-32",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-12",
            0
          ],
          "destination": [
            "obj-33",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-33",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-14",
            0
          ],
          "destination": [
            "obj-34",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-34",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-16",
            0
          ],
          "destination": [
            "obj-35",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-35",
            0
          ],
          "destination": [
            "obj-19",
            0
          ]
        }
      }
    ]
  }
}
