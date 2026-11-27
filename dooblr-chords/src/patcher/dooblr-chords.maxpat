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
      100,
      100,
      1280,
      640
    ],
    "openrect": [
      0,
      0,
      0,
      169
    ],
    "bglocked": 0,
    "openinpresentation": 1,
    "default_fontsize": 10,
    "default_fontface": 0,
    "default_fontname": "Arial",
    "gridonopen": 1,
    "gridsize": [
      8,
      8
    ],
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
    "boxanimatetime": 200,
    "enablehscroll": 1,
    "enablevscroll": 1,
    "devicewidth": 400,
    "description": "Chord progression generator with CLIP and LIVE modes, a Spice/Length main view, and a harmony bus for dooblr-chord-receiver.",
    "digest": "",
    "tags": "",
    "style": "",
    "subpatcher_template": "",
    "assistshowspatchername": 0,
    "boxes": [
      {
        "box": {
          "maxclass": "panel",
          "numinlets": 1,
          "numoutlets": 0,
          "mode": 0,
          "border": 0,
          "rounded": 0,
          "background": 1,
          "ignoreclick": 1,
          "bgcolor": [
            0.086,
            0.094,
            0.11,
            1
          ],
          "bgfillcolor_type": "color",
          "bgfillcolor_color": [
            0.086,
            0.094,
            0.11,
            1
          ],
          "id": "obj-1",
          "patching_rect": [
            0,
            0,
            400,
            169
          ],
          "presentation": 1,
          "presentation_rect": [
            0,
            0,
            400,
            169
          ]
        }
      },
      {
        "box": {
          "maxclass": "panel",
          "numinlets": 1,
          "numoutlets": 0,
          "mode": 0,
          "border": 0,
          "rounded": 0,
          "background": 1,
          "ignoreclick": 1,
          "bgcolor": [
            0.114,
            0.125,
            0.149,
            1
          ],
          "bgfillcolor_type": "color",
          "bgfillcolor_color": [
            0.114,
            0.125,
            0.149,
            1
          ],
          "id": "obj-2",
          "patching_rect": [
            0,
            0,
            112,
            169
          ],
          "presentation": 1,
          "presentation_rect": [
            0,
            0,
            112,
            169
          ]
        }
      },
      {
        "box": {
          "maxclass": "panel",
          "numinlets": 1,
          "numoutlets": 0,
          "mode": 0,
          "border": 0,
          "rounded": 0,
          "background": 1,
          "ignoreclick": 1,
          "bgcolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "bgfillcolor_type": "color",
          "bgfillcolor_color": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "id": "obj-3",
          "patching_rect": [
            4,
            6,
            3,
            28
          ],
          "presentation": 1,
          "presentation_rect": [
            4,
            6,
            3,
            28
          ]
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "dooblr-chords",
          "fontname": "Arial Bold",
          "fontsize": 11,
          "textcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "id": "obj-4",
          "patching_rect": [
            8,
            4,
            100,
            14
          ],
          "presentation": 1,
          "presentation_rect": [
            8,
            4,
            100,
            14
          ]
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "v0.8.0",
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
          "id": "obj-5",
          "patching_rect": [
            8,
            18,
            50,
            12
          ],
          "presentation": 1,
          "presentation_rect": [
            8,
            18,
            50,
            12
          ]
        }
      },
      {
        "box": {
          "maxclass": "live.text",
          "varname": "generate",
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "",
            ""
          ],
          "parameter_enable": 1,
          "mode": 0,
          "text": "Generate",
          "texton": "Generate",
          "fontname": "Arial Bold",
          "fontsize": 10,
          "activebgcolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "activebgoncolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activetextcolor": [
            0.086,
            0.094,
            0.11,
            1
          ],
          "activetextoncolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "bgcolor": [
            0.373,
            0.702,
            0.631,
            0.38
          ],
          "bgoncolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "textcolor": [
            0.086,
            0.094,
            0.11,
            0.38
          ],
          "textoncolor": [
            0.902,
            0.91,
            0.922,
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
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Generate",
              "parameter_shortname": "Generate",
              "parameter_type": 2,
              "parameter_enum": [
                "off",
                "on"
              ],
              "parameter_mmax": 1,
              "parameter_invisible": 2
            }
          },
          "annotation_name": "Generate",
          "annotation": "Generate makes a new progression with a new seed and writes it as a clip in the next empty slot on this track.",
          "id": "obj-6",
          "patching_rect": [
            8,
            224,
            100,
            20
          ],
          "presentation": 1,
          "presentation_rect": [
            8,
            52,
            100,
            20
          ]
        }
      },
      {
        "box": {
          "maxclass": "live.text",
          "varname": "reroll",
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "",
            ""
          ],
          "parameter_enable": 1,
          "mode": 0,
          "text": "Reroll",
          "texton": "Reroll",
          "fontname": "Arial Bold",
          "fontsize": 11,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activebgoncolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activetextcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activetextoncolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "bgcolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "bgoncolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            0.38
          ],
          "textoncolor": [
            0.902,
            0.91,
            0.922,
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
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Reroll",
              "parameter_shortname": "Reroll",
              "parameter_type": 2,
              "parameter_enum": [
                "off",
                "on"
              ],
              "parameter_mmax": 1,
              "parameter_invisible": 2
            }
          },
          "annotation_name": "Reroll",
          "annotation": "Reroll keeps the locked chords, regenerates the others with a new seed and writes a clip.",
          "id": "obj-7",
          "patching_rect": [
            8,
            296,
            100,
            16
          ],
          "presentation": 1,
          "presentation_rect": [
            8,
            114,
            100,
            16
          ]
        }
      },
      {
        "box": {
          "maxclass": "panel",
          "numinlets": 1,
          "numoutlets": 0,
          "mode": 0,
          "border": 0,
          "rounded": 0,
          "background": 1,
          "ignoreclick": 1,
          "bgcolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "bgfillcolor_type": "color",
          "bgfillcolor_color": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "id": "obj-12",
          "patching_rect": [
            112,
            0,
            1,
            169
          ],
          "presentation": 1,
          "presentation_rect": [
            112,
            0,
            1,
            169
          ]
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "KEY",
          "fontname": "Arial Bold",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "id": "obj-13",
          "patching_rect": [
            116,
            4,
            28,
            12
          ],
          "presentation": 1,
          "presentation_rect": [
            116,
            4,
            28,
            12
          ],
          "varname": "key_label"
        }
      },
      {
        "box": {
          "maxclass": "live.menu",
          "varname": "root",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
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
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Root",
              "parameter_shortname": "Root",
              "parameter_type": 2,
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
              "parameter_initial_enable": 1,
              "parameter_invisible": 0
            }
          },
          "annotation_name": "Root",
          "annotation": "Root sets the key centre of the progression.",
          "id": "obj-16",
          "patching_rect": [
            116,
            40,
            44,
            16
          ],
          "presentation": 1,
          "presentation_rect": [
            116,
            16,
            44,
            20
          ]
        }
      },
      {
        "box": {
          "maxclass": "live.menu",
          "varname": "mode",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
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
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Mode",
              "parameter_shortname": "Mode",
              "parameter_type": 2,
              "parameter_enum": [
                "Major",
                "Natural Minor",
                "Dorian",
                "Phrygian",
                "Lydian",
                "Mixolydian",
                "Harmonic Minor"
              ],
              "parameter_mmax": 6,
              "parameter_initial": [
                1
              ],
              "parameter_initial_enable": 1,
              "parameter_invisible": 0
            }
          },
          "annotation_name": "Mode",
          "annotation": "Mode sets the scale the chords are built from.",
          "id": "obj-17",
          "patching_rect": [
            162,
            40,
            80,
            16
          ],
          "presentation": 1,
          "presentation_rect": [
            162,
            16,
            100,
            20
          ]
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "LENGTH",
          "fontname": "Arial Bold",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "id": "obj-18",
          "patching_rect": [
            252,
            38,
            140,
            12
          ],
          "presentation": 1,
          "presentation_rect": [
            252,
            36,
            140,
            12
          ],
          "varname": "length_label"
        }
      },
      {
        "box": {
          "maxclass": "live.dial",
          "varname": "length",
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "",
            "float"
          ],
          "parameter_enable": 1,
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
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Length",
              "parameter_shortname": "Length",
              "parameter_type": 1,
              "parameter_mmin": 2,
              "parameter_mmax": 16,
              "parameter_initial": [
                4
              ],
              "parameter_initial_enable": 1,
              "parameter_unitstyle": 0,
              "parameter_invisible": 0
            }
          },
          "annotation_name": "Length",
          "annotation": "Length is the number of chords in the progression.",
          "id": "obj-20",
          "patching_rect": [
            400,
            40,
            70,
            76
          ],
          "presentation": 1,
          "presentation_rect": [
            252,
            48,
            140,
            80
          ],
          "dialcolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "activedialcolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "needlecolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activeneedlecolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "panelcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "showname": 0,
          "shownumber": 1
        }
      },
      {
        "box": {
          "maxclass": "jsui",
          "filename": "dooblr_blocks.js",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "parameter_enable": 0,
          "border": 0,
          "annotation_name": "Progression",
          "annotation": "Click a chord to lock it. Locked chords stay the same when you press Reroll.",
          "id": "obj-38",
          "patching_rect": [
            116,
            128,
            276,
            37
          ],
          "presentation": 1,
          "presentation_rect": [
            116,
            128,
            276,
            37
          ],
          "varname": "blocks"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "live.thisdevice",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "bang",
            "int",
            "int"
          ],
          "id": "obj-39",
          "patching_rect": [
            800,
            20,
            90,
            22
          ]
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
          "id": "obj-40",
          "patching_rect": [
            800,
            52,
            34,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "js dooblr_chords.js",
          "numinlets": 1,
          "numoutlets": 4,
          "outlettype": [
            "",
            "",
            "",
            ""
          ],
          "saved_object_attributes": {
            "filename": "dooblr_chords.js",
            "parameter_enable": 0
          },
          "id": "obj-41",
          "patching_rect": [
            800,
            420,
            200,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param root",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-42",
          "patching_rect": [
            800,
            90,
            140,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param mode",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-43",
          "patching_rect": [
            950,
            90,
            140,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param length",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-44",
          "patching_rect": [
            1100,
            90,
            140,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "route bang 1",
          "numinlets": 2,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            ""
          ],
          "id": "obj-54",
          "patching_rect": [
            800,
            200,
            80,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "message",
          "text": "generate",
          "numinlets": 2,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-55",
          "patching_rect": [
            800,
            232,
            70,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "route bang 1",
          "numinlets": 2,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            ""
          ],
          "id": "obj-56",
          "patching_rect": [
            950,
            200,
            80,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "message",
          "text": "reroll",
          "numinlets": 2,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-57",
          "patching_rect": [
            950,
            232,
            70,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "midiin",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            "int"
          ],
          "id": "obj-60",
          "patching_rect": [
            1100,
            20,
            50,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "midiout",
          "numinlets": 1,
          "numoutlets": 0,
          "id": "obj-61",
          "patching_rect": [
            1100,
            60,
            55,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "text": "Controls send \"param <name> <value>\" to the js. The Advanced window (p advanced) sends those plus write, dropfile, useclip and clearsource through its outlet. ADVANCED sends openadvanced. js sends setstate to the display, set <seed> to the Seed box, MIDI to midiout, and harmony JSON to send dooblr_chords_bus.",
          "numinlets": 1,
          "numoutlets": 0,
          "fontsize": 10,
          "id": "obj-62",
          "patching_rect": [
            800,
            460,
            520,
            48
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "thispatcher",
          "varname": "thispatcher",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-88",
          "patching_rect": [
            800,
            500,
            90,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "live.text",
          "varname": "lead",
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "",
            ""
          ],
          "parameter_enable": 1,
          "mode": 1,
          "text": "LEAD",
          "texton": "LEAD",
          "fontname": "Arial Bold",
          "fontsize": 12,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activebgoncolor": [
            0.851,
            0.639,
            0.357,
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
            0.851,
            0.639,
            0.357,
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
            0.851,
            0.639,
            0.357,
            1
          ],
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Lead",
              "parameter_shortname": "Lead",
              "parameter_type": 2,
              "parameter_enum": [
                "off",
                "on"
              ],
              "parameter_mmax": 1,
              "parameter_initial": [
                0
              ],
              "parameter_initial_enable": 1,
              "parameter_invisible": 0
            }
          },
          "annotation_name": "Lead",
          "annotation": "LEAD writes a monophonic melody over the chord progression instead of the chords themselves. The same seed, key, length and locks shape the line.",
          "id": "obj-89",
          "patching_rect": [
            8,
            272,
            100,
            18
          ],
          "presentation": 1,
          "presentation_rect": [
            8,
            94,
            100,
            18
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param lead",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-91",
          "patching_rect": [
            1550,
            150,
            140,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "",
          "fontname": "Arial Bold",
          "fontsize": 9,
          "textcolor": [
            0.541,
            0.569,
            0.612,
            1
          ],
          "ignoreclick": 1,
          "background": 0,
          "id": "obj-77",
          "patching_rect": [
            268,
            16,
            124,
            20
          ],
          "presentation": 1,
          "presentation_rect": [
            268,
            16,
            124,
            20
          ],
          "varname": "srcbadge"
        }
      },
      {
        "box": {
          "maxclass": "live.text",
          "varname": "open_advanced",
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "",
            ""
          ],
          "parameter_enable": 0,
          "mode": 0,
          "text": "ADVANCED",
          "texton": "ADVANCED",
          "fontname": "Arial",
          "fontsize": 8.5,
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
            0.541,
            0.569,
            0.612,
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
            0.541,
            0.569,
            0.612,
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
          "annotation_name": "Advanced",
          "annotation": "Opens the Advanced panel: Bars per Chord, voicing, velocity, humanize, seed and Write.",
          "id": "obj-80",
          "patching_rect": [
            8,
            320,
            100,
            18
          ],
          "presentation": 1,
          "presentation_rect": [
            8,
            132,
            100,
            18
          ]
        }
      },
      {
        "box": {
          "maxclass": "live.tab",
          "varname": "playmode",
          "numinlets": 1,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            "float"
          ],
          "parameter_enable": 1,
          "num_lines_patching": 1,
          "num_lines_presentation": 1,
          "fontname": "Arial Bold",
          "fontsize": 11,
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
          "annotation_name": "Play Mode",
          "annotation": "CLIP writes Session clips when you press Generate. LIVE plays the generated progression as MIDI in real time, synced to Live's transport, and does not write clips. Incoming MIDI still passes through.",
          "id": "obj-96",
          "patching_rect": [
            8,
            200,
            100,
            18
          ],
          "presentation": 1,
          "presentation_rect": [
            8,
            32,
            100,
            18
          ],
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Play Mode",
              "parameter_shortname": "Play Mode",
              "parameter_type": 2,
              "parameter_enum": [
                "CLIP",
                "LIVE"
              ],
              "parameter_mmax": 1,
              "parameter_initial": [
                0
              ],
              "parameter_initial_enable": 1,
              "parameter_invisible": 0
            }
          }
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param playmode",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-99",
          "patching_rect": [
            10,
            230,
            160,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "live.text",
          "varname": "commit",
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "",
            ""
          ],
          "parameter_enable": 1,
          "mode": 0,
          "text": "TO CLIP",
          "texton": "TO CLIP",
          "fontname": "Arial Bold",
          "fontsize": 10,
          "activebgcolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activebgoncolor": [
            0.145,
            0.161,
            0.192,
            1
          ],
          "activetextcolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activetextoncolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "bgcolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "bgoncolor": [
            0.145,
            0.161,
            0.192,
            0.38
          ],
          "textcolor": [
            0.902,
            0.91,
            0.922,
            0.38
          ],
          "textoncolor": [
            0.902,
            0.91,
            0.922,
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
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "TO CLIP",
              "parameter_shortname": "TO CLIP",
              "parameter_type": 2,
              "parameter_enum": [
                "off",
                "on"
              ],
              "parameter_mmax": 1,
              "parameter_invisible": 2
            }
          },
          "annotation_name": "TO CLIP",
          "annotation": "TO CLIP writes the progression as it is currently playing in LIVE mode \u2014 including Evolve mutations and settings already applied \u2014 into the next empty Session clip slot. Live playback is not stopped. Dimmed in CLIP mode.",
          "id": "obj-101",
          "patching_rect": [
            8,
            248,
            100,
            18
          ],
          "presentation": 1,
          "presentation_rect": [
            8,
            74,
            100,
            18
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "route bang 1",
          "numinlets": 2,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            ""
          ],
          "id": "obj-104",
          "patching_rect": [
            1250,
            200,
            80,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "message",
          "text": "commit",
          "numinlets": 2,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-105",
          "patching_rect": [
            1250,
            232,
            70,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "send dooblr_chords_bus",
          "numinlets": 1,
          "numoutlets": 0,
          "id": "obj-107",
          "patching_rect": [
            1000,
            460,
            160,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "receive dooblr_chords_bus",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-108",
          "patching_rect": [
            1000,
            500,
            170,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend bus",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "id": "obj-109",
          "patching_rect": [
            1000,
            532,
            90,
            22
          ]
        }
      },
      {
        "box": {
          "maxclass": "live.dial",
          "varname": "spice",
          "numinlets": 1,
          "numoutlets": 2,
          "outlettype": [
            "",
            "float"
          ],
          "parameter_enable": 1,
          "showname": 0,
          "shownumber": 1,
          "dialcolor": [
            0.173,
            0.188,
            0.22,
            1
          ],
          "activedialcolor": [
            0.373,
            0.702,
            0.631,
            1
          ],
          "needlecolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "activeneedlecolor": [
            0.902,
            0.91,
            0.922,
            1
          ],
          "panelcolor": [
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
          "fontname": "Arial",
          "fontsize": 11,
          "saved_attribute_attributes": {
            "valueof": {
              "parameter_longname": "Spice",
              "parameter_shortname": "Spice",
              "parameter_type": 1,
              "parameter_mmin": 0,
              "parameter_mmax": 100,
              "parameter_initial": [
                35
              ],
              "parameter_initial_enable": 1,
              "parameter_unitstyle": 5,
              "parameter_invisible": 0
            }
          },
          "annotation_name": "Spice",
          "annotation": "Spice is a macro for how adventurous the progression is: it sets Color and Chromatic together. The individual Color and Chromatic knobs stay in the Advanced window.",
          "id": "obj-110",
          "patching_rect": [
            300,
            40,
            70,
            76
          ],
          "presentation": 1,
          "presentation_rect": [
            116,
            48,
            124,
            80
          ]
        }
      },
      {
        "box": {
          "maxclass": "comment",
          "varname": "spice_heading",
          "numinlets": 1,
          "numoutlets": 0,
          "text": "SPICE",
          "fontname": "Arial Bold",
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
            116,
            36,
            124,
            12
          ],
          "patching_rect": [
            116,
            38,
            124,
            12
          ],
          "id": "obj-111"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "prepend param spice",
          "numinlets": 1,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "presentation": 0,
          "patching_rect": [
            300,
            130,
            140,
            22
          ],
          "id": "obj-112"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "route bang 1",
          "numinlets": 2,
          "numoutlets": 3,
          "outlettype": [
            "",
            "",
            ""
          ],
          "presentation": 0,
          "patching_rect": [
            8,
            360,
            80,
            22
          ],
          "id": "obj-113"
        }
      },
      {
        "box": {
          "maxclass": "message",
          "text": "openadvanced",
          "numinlets": 2,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "presentation": 0,
          "patching_rect": [
            8,
            388,
            90,
            22
          ],
          "id": "obj-114"
        }
      },
      {
        "box": {
          "maxclass": "newobj",
          "text": "p advanced",
          "varname": "advanced",
          "numinlets": 0,
          "numoutlets": 1,
          "outlettype": [
            ""
          ],
          "presentation": 0,
          "patching_rect": [
            800,
            80,
            80,
            22
          ],
          "id": "obj-138",
          "saved_object_attributes": {
            "fontname": "Arial",
            "fontsize": 12
          },
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
              200,
              120,
              760,
              420
            ],
            "openinpresentation": 1,
            "default_fontname": "Arial",
            "default_fontsize": 10,
            "default_fontface": 0,
            "gridsize": [
              8,
              8
            ],
            "style": "",
            "title": "dooblr-chords \u00b7 Advanced",
            "toolbarvisible": 0,
            "statusbarvisible": 0,
            "enablehscroll": 0,
            "enablevscroll": 0,
            "boxes": [
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Color",
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
                  "id": "obj-23",
                  "patching_rect": [
                    12,
                    68,
                    70,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    12,
                    28,
                    70,
                    12
                  ],
                  "varname": "color_label"
                }
              },
              {
                "box": {
                  "maxclass": "live.dial",
                  "varname": "color",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "showname": 0,
                  "shownumber": 1,
                  "dialcolor": [
                    0.173,
                    0.188,
                    0.22,
                    1
                  ],
                  "activedialcolor": [
                    0.373,
                    0.702,
                    0.631,
                    1
                  ],
                  "needlecolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "activeneedlecolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "panelcolor": [
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
                  "fontname": "Arial",
                  "fontsize": 10,
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Color",
                      "parameter_shortname": "Color",
                      "parameter_type": 1,
                      "parameter_mmin": 0,
                      "parameter_mmax": 100,
                      "parameter_initial": [
                        35
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_unitstyle": 5,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Color",
                  "annotation": "Color adds sevenths, ninths and other extensions. At 0 every chord is a plain triad.",
                  "id": "obj-24",
                  "patching_rect": [
                    12,
                    80,
                    70,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    12,
                    40,
                    70,
                    76
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "live.dial",
                  "varname": "chromatic",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "showname": 0,
                  "shownumber": 1,
                  "dialcolor": [
                    0.173,
                    0.188,
                    0.22,
                    1
                  ],
                  "activedialcolor": [
                    0.851,
                    0.639,
                    0.357,
                    1
                  ],
                  "needlecolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "activeneedlecolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "panelcolor": [
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
                  "fontname": "Arial",
                  "fontsize": 10,
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Chromatic",
                      "parameter_shortname": "Chromatic",
                      "parameter_type": 1,
                      "parameter_mmin": 0,
                      "parameter_mmax": 100,
                      "parameter_initial": [
                        10
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_unitstyle": 5,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Chromatic",
                  "annotation": "Spice (the Chromatic control) adds borrowed chords, secondary dominants, tritone substitutions and the Neapolitan. At 0 every chord stays in the scale.",
                  "id": "obj-25",
                  "patching_rect": [
                    90,
                    80,
                    70,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    90,
                    40,
                    70,
                    76
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Chromatic",
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
                  "id": "obj-76",
                  "patching_rect": [
                    90,
                    68,
                    70,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    90,
                    28,
                    70,
                    12
                  ],
                  "varname": "chromatic_label"
                }
              },
              {
                "box": {
                  "maxclass": "live.text",
                  "varname": "write",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    ""
                  ],
                  "parameter_enable": 1,
                  "mode": 0,
                  "text": "Write",
                  "texton": "Write",
                  "fontname": "Arial Bold",
                  "fontsize": 11,
                  "activebgcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "activebgoncolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "activetextcolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "activetextoncolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "bgcolor": [
                    0.145,
                    0.161,
                    0.192,
                    0.38
                  ],
                  "bgoncolor": [
                    0.145,
                    0.161,
                    0.192,
                    0.38
                  ],
                  "textcolor": [
                    0.902,
                    0.91,
                    0.922,
                    0.38
                  ],
                  "textoncolor": [
                    0.902,
                    0.91,
                    0.922,
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
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Write",
                      "parameter_shortname": "Write",
                      "parameter_type": 2,
                      "parameter_enum": [
                        "off",
                        "on"
                      ],
                      "parameter_mmax": 1,
                      "parameter_invisible": 2
                    }
                  },
                  "annotation_name": "Write",
                  "annotation": "Write puts the progression shown below into the next empty clip slot without changing it.",
                  "id": "obj-8",
                  "patching_rect": [
                    440,
                    112,
                    108,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    460,
                    72,
                    88,
                    24
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Seed",
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
                  "id": "obj-9",
                  "patching_rect": [
                    440,
                    68,
                    108,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    460,
                    28,
                    88,
                    12
                  ],
                  "varname": "adv_seed_label"
                }
              },
              {
                "box": {
                  "maxclass": "live.numbox",
                  "varname": "seed",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "fontname": "Arial",
                  "fontsize": 11,
                  "activebgcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "textcolor": [
                    0.541,
                    0.569,
                    0.612,
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
                  "activeslidercolor": [
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
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Seed",
                      "parameter_shortname": "Seed",
                      "parameter_type": 1,
                      "parameter_mmin": 1,
                      "parameter_mmax": 99999,
                      "parameter_initial": [
                        1
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_unitstyle": 0,
                      "parameter_invisible": 1
                    }
                  },
                  "annotation_name": "Seed",
                  "annotation": "The seed makes a progression repeatable. The same seed and settings always give the same chords.",
                  "id": "obj-10",
                  "patching_rect": [
                    440,
                    80,
                    108,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    460,
                    40,
                    88,
                    24
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Bars per Chord",
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
                  "id": "obj-19",
                  "patching_rect": [
                    172,
                    108,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    172,
                    68,
                    128,
                    12
                  ],
                  "varname": "adv_bars_label"
                }
              },
              {
                "box": {
                  "maxclass": "live.tab",
                  "varname": "duration",
                  "numinlets": 1,
                  "numoutlets": 3,
                  "outlettype": [
                    "",
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "num_lines_patching": 1,
                  "num_lines_presentation": 1,
                  "fontname": "Arial",
                  "fontsize": 11,
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
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Duration",
                      "parameter_shortname": "Duration",
                      "parameter_type": 2,
                      "parameter_enum": [
                        "1/2",
                        "1",
                        "2"
                      ],
                      "parameter_mmax": 2,
                      "parameter_initial": [
                        1
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Bars per Chord",
                  "annotation": "Bars per Chord sets how long each chord lasts: half a bar, one bar or two bars.",
                  "id": "obj-21",
                  "patching_rect": [
                    172,
                    120,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    172,
                    80,
                    128,
                    24
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "ADVANCED",
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
                  "id": "obj-27",
                  "patching_rect": [
                    12,
                    48,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    12,
                    8,
                    240,
                    16
                  ],
                  "varname": "adv_title"
                }
              },
              {
                "box": {
                  "maxclass": "live.dial",
                  "varname": "register",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "showname": 1,
                  "shownumber": 1,
                  "dialcolor": [
                    0.173,
                    0.188,
                    0.22,
                    1
                  ],
                  "activedialcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "needlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "activeneedlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "panelcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "textcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "fontname": "Arial",
                  "fontsize": 9,
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Register",
                      "parameter_shortname": "Register",
                      "parameter_type": 1,
                      "parameter_mmin": 36,
                      "parameter_mmax": 72,
                      "parameter_initial": [
                        60
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_unitstyle": 8,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Register",
                  "annotation": "Register sets the lowest note of the upper voices.",
                  "id": "obj-28",
                  "patching_rect": [
                    12,
                    164,
                    56,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    12,
                    124,
                    56,
                    72
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "live.dial",
                  "varname": "spread",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "showname": 1,
                  "shownumber": 1,
                  "dialcolor": [
                    0.173,
                    0.188,
                    0.22,
                    1
                  ],
                  "activedialcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "needlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "activeneedlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "panelcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "textcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "fontname": "Arial",
                  "fontsize": 9,
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Spread",
                      "parameter_shortname": "Spread",
                      "parameter_type": 1,
                      "parameter_mmin": 0,
                      "parameter_mmax": 100,
                      "parameter_initial": [
                        25
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_unitstyle": 5,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Spread",
                  "annotation": "Spread opens the voicing from close position to drop-2 to wide open.",
                  "id": "obj-29",
                  "patching_rect": [
                    76,
                    164,
                    56,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    76,
                    124,
                    56,
                    72
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Bass",
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
                  "id": "obj-30",
                  "patching_rect": [
                    336,
                    164,
                    48,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    336,
                    124,
                    48,
                    14
                  ],
                  "varname": "bass_label"
                }
              },
              {
                "box": {
                  "maxclass": "live.text",
                  "varname": "bass",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    ""
                  ],
                  "parameter_enable": 1,
                  "mode": 1,
                  "text": "Off",
                  "texton": "On",
                  "fontname": "Arial",
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
                    0.541,
                    0.569,
                    0.612,
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
                    0.541,
                    0.569,
                    0.612,
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
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Bass",
                      "parameter_shortname": "Bass",
                      "parameter_type": 2,
                      "parameter_enum": [
                        "off",
                        "on"
                      ],
                      "parameter_mmax": 1,
                      "parameter_initial": [
                        1
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Bass",
                  "annotation": "Bass adds a separate bass note an octave or more below the chord.",
                  "id": "obj-31",
                  "patching_rect": [
                    336,
                    180,
                    48,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    336,
                    140,
                    48,
                    22
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "live.dial",
                  "varname": "velocity",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "showname": 1,
                  "shownumber": 1,
                  "dialcolor": [
                    0.173,
                    0.188,
                    0.22,
                    1
                  ],
                  "activedialcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "needlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "activeneedlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "panelcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "textcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "fontname": "Arial",
                  "fontsize": 9,
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Velocity",
                      "parameter_shortname": "Velocity",
                      "parameter_type": 1,
                      "parameter_mmin": 1,
                      "parameter_mmax": 127,
                      "parameter_initial": [
                        100
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_unitstyle": 0,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Velocity",
                  "annotation": "Velocity sets how hard every note is played.",
                  "id": "obj-34",
                  "patching_rect": [
                    140,
                    164,
                    56,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    140,
                    124,
                    56,
                    72
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "live.dial",
                  "varname": "humanize",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "showname": 1,
                  "shownumber": 1,
                  "dialcolor": [
                    0.173,
                    0.188,
                    0.22,
                    1
                  ],
                  "activedialcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "needlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "activeneedlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "panelcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "textcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "fontname": "Arial",
                  "fontsize": 9,
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Humanize",
                      "parameter_shortname": "Humanize",
                      "parameter_type": 1,
                      "parameter_mmin": 0,
                      "parameter_mmax": 100,
                      "parameter_initial": [
                        0
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_unitstyle": 5,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Humanize",
                  "annotation": "Humanize nudges note timing and velocity a little. At 0 the notes are exactly on the grid.",
                  "id": "obj-35",
                  "patching_rect": [
                    204,
                    164,
                    56,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    204,
                    124,
                    56,
                    72
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "SOURCE",
                  "fontname": "Arial Bold",
                  "fontsize": 10,
                  "textcolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "ignoreclick": 1,
                  "background": 0,
                  "id": "obj-64",
                  "patching_rect": [
                    12,
                    244,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    12,
                    204,
                    200,
                    14
                  ],
                  "varname": "src_title"
                }
              },
              {
                "box": {
                  "maxclass": "live.drop",
                  "varname": "srcdrop",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    ""
                  ],
                  "parameter_enable": 1,
                  "legend": "Drop a .mid file",
                  "fontname": "Arial",
                  "fontsize": 10,
                  "bgcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "bordercolor": [
                    0.373,
                    0.702,
                    0.631,
                    1
                  ],
                  "focusbordercolor": [
                    0.373,
                    0.702,
                    0.631,
                    1
                  ],
                  "textcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Source File",
                      "parameter_shortname": "Source",
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Source File",
                  "annotation": "Drop a MIDI file (.mid) here to use its chords as the source. VAR and B then write a variation or a B section of it. The file path is saved with the Set.",
                  "id": "obj-65",
                  "patching_rect": [
                    12,
                    260,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    12,
                    220,
                    248,
                    52
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "live.text",
                  "varname": "useclip",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    ""
                  ],
                  "parameter_enable": 1,
                  "mode": 0,
                  "text": "USE CLIP",
                  "texton": "USE CLIP",
                  "fontname": "Arial Bold",
                  "fontsize": 11,
                  "activebgcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "activebgoncolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "activetextcolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "activetextoncolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "bgcolor": [
                    0.145,
                    0.161,
                    0.192,
                    0.38
                  ],
                  "bgoncolor": [
                    0.145,
                    0.161,
                    0.192,
                    0.38
                  ],
                  "textcolor": [
                    0.902,
                    0.91,
                    0.922,
                    0.38
                  ],
                  "textoncolor": [
                    0.902,
                    0.91,
                    0.922,
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
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Use Clip",
                      "parameter_shortname": "Use Clip",
                      "parameter_type": 2,
                      "parameter_enum": [
                        "off",
                        "on"
                      ],
                      "parameter_mmax": 1,
                      "parameter_invisible": 2
                    }
                  },
                  "annotation_name": "Use Clip",
                  "annotation": "USE CLIP reads the MIDI clip shown in the Detail View (or the selected clip) as the source.",
                  "id": "obj-66",
                  "patching_rect": [
                    272,
                    260,
                    100,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    272,
                    220,
                    100,
                    24
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "live.text",
                  "varname": "clearsource",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    ""
                  ],
                  "parameter_enable": 1,
                  "mode": 0,
                  "text": "CLEAR",
                  "texton": "CLEAR",
                  "fontname": "Arial",
                  "fontsize": 9,
                  "activebgcolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "activebgoncolor": [
                    0.145,
                    0.161,
                    0.192,
                    1
                  ],
                  "activetextcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "activetextoncolor": [
                    0.902,
                    0.91,
                    0.922,
                    1
                  ],
                  "bgcolor": [
                    0.145,
                    0.161,
                    0.192,
                    0.38
                  ],
                  "bgoncolor": [
                    0.145,
                    0.161,
                    0.192,
                    0.38
                  ],
                  "textcolor": [
                    0.541,
                    0.569,
                    0.612,
                    0.38
                  ],
                  "textoncolor": [
                    0.902,
                    0.91,
                    0.922,
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
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Clear Source",
                      "parameter_shortname": "Clear",
                      "parameter_type": 2,
                      "parameter_enum": [
                        "off",
                        "on"
                      ],
                      "parameter_mmax": 1,
                      "parameter_invisible": 2
                    }
                  },
                  "annotation_name": "Clear Source",
                  "annotation": "CLEAR forgets the source, so Generate makes fresh progressions again.",
                  "id": "obj-67",
                  "patching_rect": [
                    380,
                    260,
                    64,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    380,
                    220,
                    64,
                    24
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "live.tab",
                  "varname": "kind",
                  "numinlets": 1,
                  "numoutlets": 3,
                  "outlettype": [
                    "",
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "num_lines_patching": 1,
                  "num_lines_presentation": 1,
                  "fontname": "Arial Bold",
                  "fontsize": 11,
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
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Source Kind",
                      "parameter_shortname": "Kind",
                      "parameter_type": 2,
                      "parameter_enum": [
                        "VAR",
                        "B"
                      ],
                      "parameter_mmax": 1,
                      "parameter_initial": [
                        0
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "VAR / B",
                  "annotation": "VAR writes a variation of the source: same key, length and rhythm, some chords swapped. B writes a contrasting B section that leads back to the source.",
                  "id": "obj-68",
                  "patching_rect": [
                    272,
                    288,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    272,
                    248,
                    172,
                    24
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Drop a .mid file or press USE CLIP.",
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
                  "id": "obj-69",
                  "patching_rect": [
                    12,
                    316,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    12,
                    276,
                    536,
                    18
                  ],
                  "varname": "srcinfo"
                }
              },
              {
                "box": {
                  "maxclass": "live.dial",
                  "varname": "density",
                  "numinlets": 1,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "showname": 1,
                  "shownumber": 1,
                  "dialcolor": [
                    0.173,
                    0.188,
                    0.22,
                    1
                  ],
                  "activedialcolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "needlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "activeneedlecolor": [
                    0.541,
                    0.569,
                    0.612,
                    1
                  ],
                  "panelcolor": [
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
                  "fontname": "Arial",
                  "fontsize": 9,
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Density",
                      "parameter_shortname": "Density",
                      "parameter_type": 1,
                      "parameter_mmin": 0,
                      "parameter_mmax": 100,
                      "parameter_initial": [
                        50
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_unitstyle": 5,
                      "parameter_invisible": 0
                    }
                  },
                  "annotation_name": "Density",
                  "annotation": "Density sets how busy the lead line is: higher values use shorter notes and fewer rests. It only applies when Lead is on.",
                  "id": "obj-90",
                  "patching_rect": [
                    268,
                    164,
                    56,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    268,
                    124,
                    56,
                    72
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "live.tab",
                  "varname": "notes",
                  "numinlets": 1,
                  "numoutlets": 3,
                  "outlettype": [
                    "",
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "num_lines_patching": 1,
                  "num_lines_presentation": 1,
                  "fontname": "Arial",
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
                  "annotation_name": "Notes",
                  "annotation": "Notes sets the chord type: a triad (3 notes), 7th (4), 9th (5) or 11th (6). Color and Spice still colour the tones, but they do not change how many notes each chord has. When Bass is on, the bass is one of those notes (it takes the root out of the upper voicing). A slash bass is also one of the N, so a triad inversion stays three notes.",
                  "id": "obj-93",
                  "patching_rect": [
                    172,
                    80,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    172,
                    40,
                    128,
                    24
                  ],
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Notes",
                      "parameter_shortname": "Notes",
                      "parameter_type": 2,
                      "parameter_enum": [
                        "TRI",
                        "7TH",
                        "9TH",
                        "11TH"
                      ],
                      "parameter_mmax": 3,
                      "parameter_initial": [
                        0
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_invisible": 0
                    }
                  }
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Notes",
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
                  "id": "obj-94",
                  "patching_rect": [
                    172,
                    68,
                    120,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    172,
                    28,
                    128,
                    12
                  ],
                  "varname": "notes_label"
                }
              },
              {
                "box": {
                  "maxclass": "live.tab",
                  "varname": "evolve",
                  "numinlets": 1,
                  "numoutlets": 3,
                  "outlettype": [
                    "",
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "num_lines_patching": 1,
                  "num_lines_presentation": 1,
                  "fontname": "Arial",
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
                  "annotation_name": "Evolve",
                  "annotation": "EVOLVE (LIVE only) lightly varies one or two inner unlocked chords at cycle boundaries: Off, every cycle, every 2 cycles, or every 4 cycles. The first and last chords stay put. Deterministic for a given seed and cycle.",
                  "id": "obj-97",
                  "patching_rect": [
                    312,
                    80,
                    116,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    312,
                    40,
                    140,
                    24
                  ],
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Evolve",
                      "parameter_shortname": "Evolve",
                      "parameter_type": 2,
                      "parameter_enum": [
                        "Off",
                        "1 cycle",
                        "2 cycles",
                        "4 cycles"
                      ],
                      "parameter_mmax": 3,
                      "parameter_initial": [
                        0
                      ],
                      "parameter_initial_enable": 1,
                      "parameter_invisible": 0
                    }
                  }
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Evolve",
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
                  "id": "obj-98",
                  "patching_rect": [
                    312,
                    68,
                    116,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    312,
                    28,
                    140,
                    12
                  ],
                  "varname": "evolve_label"
                }
              },
              {
                "box": {
                  "maxclass": "live.tab",
                  "varname": "buschan",
                  "numinlets": 1,
                  "numoutlets": 3,
                  "outlettype": [
                    "",
                    "",
                    "float"
                  ],
                  "parameter_enable": 1,
                  "num_lines_patching": 1,
                  "num_lines_presentation": 1,
                  "fontname": "Arial",
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
                  "annotation_name": "Bus Channel",
                  "annotation": "Bus Channel (A, B, C or D) is the harmony link that dooblr-chord-receiver devices listen to. Use a different letter when two dooblr-chords devices share a Set.",
                  "id": "obj-102",
                  "patching_rect": [
                    312,
                    120,
                    116,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    312,
                    80,
                    140,
                    24
                  ],
                  "saved_attribute_attributes": {
                    "valueof": {
                      "parameter_longname": "Bus Channel",
                      "parameter_shortname": "Bus",
                      "parameter_type": 2,
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
                      "parameter_initial_enable": 1,
                      "parameter_invisible": 0
                    }
                  }
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Bus Channel",
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
                  "id": "obj-103",
                  "patching_rect": [
                    312,
                    108,
                    116,
                    22
                  ],
                  "presentation": 1,
                  "presentation_rect": [
                    312,
                    68,
                    140,
                    12
                  ],
                  "varname": "buschan_label"
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "outlet",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    600,
                    50,
                    22
                  ],
                  "id": "obj-115"
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param color",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    420,
                    150,
                    22
                  ],
                  "id": "obj-116",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param chromatic",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    442,
                    150,
                    22
                  ],
                  "id": "obj-117",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param notes",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    464,
                    150,
                    22
                  ],
                  "id": "obj-118",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param duration",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    486,
                    150,
                    22
                  ],
                  "id": "obj-119",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param seed",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    508,
                    150,
                    22
                  ],
                  "id": "obj-120",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param register",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    530,
                    150,
                    22
                  ],
                  "id": "obj-121",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param spread",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    552,
                    150,
                    22
                  ],
                  "id": "obj-122",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param velocity",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    574,
                    150,
                    22
                  ],
                  "id": "obj-123",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param humanize",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    596,
                    150,
                    22
                  ],
                  "id": "obj-124",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param density",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    618,
                    150,
                    22
                  ],
                  "id": "obj-125",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param bass",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    640,
                    150,
                    22
                  ],
                  "id": "obj-126",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param evolve",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    662,
                    150,
                    22
                  ],
                  "id": "obj-127",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param buschan",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    684,
                    150,
                    22
                  ],
                  "id": "obj-128",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend param kind",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    20,
                    706,
                    150,
                    22
                  ],
                  "id": "obj-129",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "prepend dropfile",
                  "numinlets": 1,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    200,
                    420,
                    110,
                    22
                  ],
                  "id": "obj-130",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "route bang 1",
                  "numinlets": 2,
                  "numoutlets": 3,
                  "presentation": 0,
                  "patching_rect": [
                    400,
                    440,
                    80,
                    22
                  ],
                  "id": "obj-131",
                  "outlettype": [
                    "",
                    "",
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "message",
                  "text": "write",
                  "numinlets": 2,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    500,
                    440,
                    80,
                    22
                  ],
                  "id": "obj-132",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "route bang 1",
                  "numinlets": 2,
                  "numoutlets": 3,
                  "presentation": 0,
                  "patching_rect": [
                    400,
                    470,
                    80,
                    22
                  ],
                  "id": "obj-133",
                  "outlettype": [
                    "",
                    "",
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "message",
                  "text": "useclip",
                  "numinlets": 2,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    500,
                    470,
                    80,
                    22
                  ],
                  "id": "obj-134",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "newobj",
                  "text": "route bang 1",
                  "numinlets": 2,
                  "numoutlets": 3,
                  "presentation": 0,
                  "patching_rect": [
                    400,
                    500,
                    80,
                    22
                  ],
                  "id": "obj-135",
                  "outlettype": [
                    "",
                    "",
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "message",
                  "text": "clearsource",
                  "numinlets": 2,
                  "numoutlets": 1,
                  "presentation": 0,
                  "patching_rect": [
                    500,
                    500,
                    80,
                    22
                  ],
                  "id": "obj-136",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "maxclass": "comment",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "text": "Advanced window \u2014 live.* params remain device parameters.",
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
                    20,
                    380,
                    400,
                    16
                  ],
                  "id": "obj-137"
                }
              }
            ],
            "lines": [
              {
                "patchline": {
                  "source": [
                    "obj-24",
                    0
                  ],
                  "destination": [
                    "obj-116",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-116",
                    0
                  ],
                  "destination": [
                    "obj-115",
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
                    "obj-117",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-117",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-93",
                    0
                  ],
                  "destination": [
                    "obj-118",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-118",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-21",
                    0
                  ],
                  "destination": [
                    "obj-119",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-119",
                    0
                  ],
                  "destination": [
                    "obj-115",
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
                    "obj-120",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-120",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-28",
                    0
                  ],
                  "destination": [
                    "obj-121",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-121",
                    0
                  ],
                  "destination": [
                    "obj-115",
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
                    "obj-122",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-122",
                    0
                  ],
                  "destination": [
                    "obj-115",
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
                    "obj-123",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-123",
                    0
                  ],
                  "destination": [
                    "obj-115",
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
                    "obj-124",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-124",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-90",
                    0
                  ],
                  "destination": [
                    "obj-125",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-125",
                    0
                  ],
                  "destination": [
                    "obj-115",
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
                    "obj-126",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-126",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-97",
                    0
                  ],
                  "destination": [
                    "obj-127",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-127",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-102",
                    0
                  ],
                  "destination": [
                    "obj-128",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-128",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-68",
                    0
                  ],
                  "destination": [
                    "obj-129",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-129",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-65",
                    0
                  ],
                  "destination": [
                    "obj-130",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-130",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-8",
                    0
                  ],
                  "destination": [
                    "obj-131",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-131",
                    0
                  ],
                  "destination": [
                    "obj-132",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-132",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-66",
                    0
                  ],
                  "destination": [
                    "obj-133",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-133",
                    0
                  ],
                  "destination": [
                    "obj-134",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-134",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-67",
                    0
                  ],
                  "destination": [
                    "obj-135",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-135",
                    0
                  ],
                  "destination": [
                    "obj-136",
                    0
                  ]
                }
              },
              {
                "patchline": {
                  "source": [
                    "obj-136",
                    0
                  ],
                  "destination": [
                    "obj-115",
                    0
                  ]
                }
              }
            ]
          }
        }
      }
    ],
    "lines": [
      {
        "patchline": {
          "source": [
            "obj-39",
            0
          ],
          "destination": [
            "obj-40",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-40",
            0
          ],
          "destination": [
            "obj-41",
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
            "obj-42",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-42",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-17",
            0
          ],
          "destination": [
            "obj-43",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-43",
            0
          ],
          "destination": [
            "obj-41",
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
            "obj-44",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-44",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-6",
            0
          ],
          "destination": [
            "obj-54",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-54",
            0
          ],
          "destination": [
            "obj-55",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-54",
            1
          ],
          "destination": [
            "obj-55",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-55",
            0
          ],
          "destination": [
            "obj-41",
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
            "obj-56",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-56",
            0
          ],
          "destination": [
            "obj-57",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-56",
            1
          ],
          "destination": [
            "obj-57",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-57",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-38",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-41",
            0
          ],
          "destination": [
            "obj-38",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-60",
            0
          ],
          "destination": [
            "obj-61",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-89",
            0
          ],
          "destination": [
            "obj-91",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-91",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-96",
            0
          ],
          "destination": [
            "obj-99",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-99",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-41",
            2
          ],
          "destination": [
            "obj-61",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-101",
            0
          ],
          "destination": [
            "obj-104",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-104",
            0
          ],
          "destination": [
            "obj-105",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-104",
            1
          ],
          "destination": [
            "obj-105",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-105",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-41",
            3
          ],
          "destination": [
            "obj-107",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-108",
            0
          ],
          "destination": [
            "obj-109",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-109",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-110",
            0
          ],
          "destination": [
            "obj-112",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-112",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-80",
            0
          ],
          "destination": [
            "obj-113",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-113",
            0
          ],
          "destination": [
            "obj-114",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-114",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "obj-138",
            0
          ],
          "destination": [
            "obj-41",
            0
          ]
        }
      }
    ],
    "dependency_cache": [],
    "latency": 0,
    "is_mpe": 0,
    "minimum_live_version": "",
    "minimum_max_version": "",
    "platform_compatibility": 0,
    "autosave": 0
  }
}
