// v8.38: unified Wi-Fi / no-Wi-Fi variants with one selectable antenna bore.
// v8.37: right-side Wi-Fi bore below buzzer, rear cover unchanged.
// v8.36: left-side 6.4mm Wi-Fi antenna bore aligned with OLED vertical centre; rear cover unchanged.
// v8.34: internal blind M2x5 OLED fasteners and outward-facing (into cavity)
// raised 1.2mm version identification; no exterior OLED screw holes.
// RFID TIME CLOCK v8.29: overall depth 20 mm (previously 25 mm).
// Maintains USB-C PCB flush seat and OLED geometry from v8.28.
// v8.27: OLED aperture TOP edge raised 1.0 mm; bottom contour and mounting unchanged.
// v8.24: Caliper width 62.2 mm; 13 mm outer bottom flats, 2.2 mm sloped segments at 30 degrees, centre flat computed.
// v8.23: OLED bottom chamfers begin 13mm inward from each outside horizontal edge; 1mm vertical rise.
// v8.22: OLED bottom chamfers are only 1mm high, not 13mm.
// v8.21: OLED glass measured 62.3 x 39mm, lower chamfers begin 13mm above bottom and bottom flat 40mm. 0.2mm perimeter clearance.
// v8.20: angled OLED edges 1mm inward each; central bottom edge 1mm down.
// v8.19: OLED lower central horizontal edge raised 2mm; 30-degree transitions recalculated.
// v8.17: USB-C external connector opening now matches kid clock 12.2 x 7.0 mm.
// v8.16: two horizontally spaced wall-mount keyholes, both retaining vertically.
// v8.14: trim ALL corner support material to the exact rounded exterior footprint.
// v8.13: wall-rooted corner ramps grow inward layer by layer without floating islands.
// v8.12: compact circular tapered corner bosses retain kid-clock screw geometry.
// v8.11: kid-clock v3.26 lid fastening system: tapered corner mounts, blind stepped pilots and countersunk 3mm cover holes.
// v8.10: match USB-C screw bores to kid-clock v3.26 1.8mm M2 pilot; 16mm spacing preserved.
// v8.8 two-piece adaptation of supplied v8.7 source. Front and cover only.
// v8.18: Both lower OLED 30-degree edges moved inward 2mm, angle unchanged.
// v8.6: align OLED board corner seats and screw holes to same datum; PCB 71.13 x 43.57.
// v8.5: OLED L seats use a symmetric 0.5mm PCB inset; screw holes remain on exact measured centres.
// v8.4: Replace OLED L-shaped spacers with four 1mm-high L-shaped PCB corner seats.
// v8.2: OLED lower slant starts 1.5mm inward on each side, preserving 30deg and 1.6mm height.
// v7.9: OLED M2.5 holes +1.5mm in Y; top-centre solder relief 17x2x2.4mm.
// v7.8: buzzer fins are full-height at the post and taper down to the front skin.
// v7.7: previous buzzer fin orientation (reversed in v7.8).
// v7.4: OLED lower-edge transitions at 30 degrees from horizontal.
// v7.3: Earlier OLED lower-edge transitions were 70 degrees.
// v7.2: 6mm outward extension of RFID engraving beyond the 45mm antenna reference.
// v6.9: Initial OLED lower step transitions used 45-degree ramps.
// v6.7: OLED lower punch-out stepped: central 38mm full height, outer lower corners rise 1.6mm.
// v6.6: OLED PCB 71.25 x 44; raised-glass punch-out 63.4 x 40.6 square.
// v6.5: buzzer posts extend 11 mm from inner front face; front/middle split lowered to retain full length.
// v6.4: front/middle split lowered so buzzer posts remain 10 mm above inner face.
// v6.3: RFID tails match buzzer Z taper (unmirrored kid_gusset).
// v6.0: fix buzzer tail Z taper, broad at front skin; upper tail up, lower tail down.
// v3.0: buzzer retaining holes and clamp updated to 30 mm centre-to-centre.
/* v2.2 reviews kid-clock USB-C PCB mounting cradle and bottom-facing connector port.
   Dimensions match mk-piclock-kids-v3_47-recess-roof-fix-6.scad.
   Verify connector orientation and BPI GPIO wire routing physically.
*/
/*
 BPI-M2 Zero RFID timeclock wall enclosure v8.24
 Units: mm. Parametric, printable starting point, NOT dimensionally verified.
 Assembly: two printable enclosure pieces: integrated front and removable rear cover.
 Select part="front", "back", "assembly", "layout", or "buzzer_clamp".
 Set wifi_antenna=true for the 6.4mm right-side antenna hole; false for solid wall.

 Verify physical boards and connector orientations before printing.
 OLED board measured 71.25 x 44 with measured hole pitch 66.5 x 39.5, RC522 nominal 59.44 x 39.89.
 BPI mount reuses proven kid-clock hole spacing and screw pilot geometry.
 OLED hole spacing and 2.9mm glass protrusion measured; OLED centre offset still unverified. RC522 hole centers estimated from supplied dimensioned perspective drawing.
 RC522 standoff height 3.5mm; adjust after measuring components.
*/
$fn = 48;
part = "assembly"; // [front,back,assembly,oled_fit_test,buzzer_clamp,layout]

// Optional right-side antenna connector hole below the beeper.
// Set true for Wi-Fi bulkhead connector; false for a solid side wall.
wifi_antenna = false; // [false:No Wi-Fi hole,true:Wi-Fi hole]
// v5.9: buzzer M2 posts increased to 10 mm; hole spacing and clamp unchanged.
// v7.6: rear-plate passive ventilation slots aligned with BPI.
// v8.30: exterior USB-C PCB pocket, 1.7mm deep, solid internal support block.
// v5.6: integrate USB-C cradle into the middle-wall print floor, not suspended above it.
// v5.4: kid-clock recessed USB-C PCB pocket and full perimeter support rail.
// v5.1: front OLED skin 2.9 mm, PCB seats directly on inner face for flush glass.
// v5.0: reverse OLED/RC522/buzzer post gusset taper so the broad end joins the skin.
// v4.9: all four BPI posts use identical M2 pilots; removed locating peg.
// v4.8: correct BPI tails to taper toward free end; broad at front skin.
// v4.6: RC522 moved upward 10 mm with its standoffs and front tap-zone marking.
// v4.5: overlapping perimeter skirts reduce light escape at both plate seams.
// v4.4: BPI centre exactly midway between lower buzzer M2 hole and inner bottom wall.
// v3.6: buzzer centre vertically aligned with OLED.
// v3.5: BPI, RC522, OLED gusset tails extend outward horizontally from each PCB centre.
// OLED uses front-installed M2 screws with internal nuts.
// OLED glass protrusion measured at 2.9mm above PCB, plus 0.4mm clearance.
// No touch sensor, recess or touch screw holes.
flat_cap_h = 0;

W = 128;
H = 140;
D = 20; // v8.29 reduced overall depth; physical connector clearance requires checking
corner = 9;
wall = 2.8;
front_skin = 2.9; // OLED glass rises 2.9 mm above its PCB, flush with outside
fit = 0.35;

// Location coordinates (front view: x left/right, y down/up).
oled_y = 38; // preserves 10.25mm top PCB clearance in shorter case
oled_x = -18;
// RC522 is centred under the OLED, leaving a clear wiring corridor.
rfid_x = oled_x;
rfid_y = -18; // raised 10 mm from v4.5; RFID standoffs and front marking follow
// BPI runs vertically below the buzzer, to the right of the RC522.
bpi_x = 38;
bpi_rotation = 90; // Rotate PCB footprint and all four mount positions together
buzzer_x = 37;
buzzer_y = oled_y; // align buzzer centre vertically with OLED centre

// Provisional electronics envelope sizes, editable after measuring parts.
oled_board_w=71.13; oled_board_h=43.57;
// Measured physical OLED glass profile (not its PCB):
// 13mm outer lower flats; 2.2mm sloped lengths at 30 degrees;
// central lower flat is derived, not guessed.
oled_glass_w = 62.2;
oled_glass_h = 39;
oled_outer_flat_w = 13;
oled_slope_length = 2.2;
oled_slope_angle = 30;
oled_slope_run = oled_slope_length*cos(oled_slope_angle);
oled_glass_chamfer_h = oled_slope_length*sin(oled_slope_angle); // 1.1mm vertical
oled_glass_bottom_flat_w = oled_glass_w - 2*(oled_outer_flat_w + oled_slope_run); // ~32.39mm
oled_glass_clearance = 0.2; // XY clearance per edge
oled_view_w = oled_glass_w + 2*oled_glass_clearance;
oled_view_h = oled_glass_h + 2*oled_glass_clearance;
oled_view_bottom_flat_w = oled_glass_bottom_flat_w + 2*oled_glass_clearance;
oled_view_chamfer_h = oled_glass_chamfer_h;
oled_opening_top_extra = 1.0; // adds clearance ONLY above the glass
assert(oled_glass_bottom_flat_w > 0, "OLED profile exceeds total glass width");
rfid_board_w=59.44; rfid_board_h=39.89;
// Narrow, shallow engraved RFID tap-zone outline extended 6mm outside the 45mm reference.
// No through-cut: maintains front plastic above the antenna.
rfid_tap_zone_w = 45;
rfid_tap_zone_h = 45;
rfid_art_margin = 6; // outward extension from nominal 45mm antenna area
rfid_mark_w = rfid_tap_zone_w+2*rfid_art_margin;
rfid_mark_h = rfid_tap_zone_h+2*rfid_art_margin;
rfid_mark_radius = 4;
rfid_mark_line = 0.9;       // FDM-friendly narrow line, ~2 lines on a 0.4 mm nozzle
rfid_mark_depth = 0.45;    // shallow engraving; no support required

bpi_board_w=65; bpi_board_h=30;

// BPI mounting geometry reused from kid-clock case
// Board: 65x30, holes measured from bottom-left: (3.5,3.5), (61.5,3.5),
// (3.5,26.5), (61.5,26.5). Centers = 58x23 mm.
bpi_holes = [[3.5,3.5],[61.5,3.5],[3.5,26.5],[61.5,26.5]];
bpi_standoff_h = 6;
bpi_standoff_od = 5.8;
bpi_standoff_pilot_d = 1.8;
fdm_layer_h = 0.24; // kid-clock rounded tapered-gusset tip
bpi_gusset_h = 4.5;
bpi_gusset_w = 1.6;
bpi_gusset_len = 6;

// RC522 measured mounting pattern (October 2026).
// 34.5mm row near IC (bottom), 25mm opposite row (top), 37.5mm between row centres.
// Both rows centred on PCB X axis. Narrow row is at PCB top in this layout.
// The board-edge placement is centred provisionally; physical verification advised.
rfid_near_ic_span = 34.5;
rfid_far_ic_span = 25.0;
rfid_row_span = 37.5;
rfid_row_y_top = (rfid_board_h - rfid_row_span)/2;
rfid_row_y_bottom = (rfid_board_h + rfid_row_span)/2;
rfid_holes_from_top_left = [
    [(rfid_board_w-rfid_far_ic_span)/2, rfid_row_y_top],
    [(rfid_board_w+rfid_far_ic_span)/2, rfid_row_y_top],
    [(rfid_board_w-rfid_near_ic_span)/2, rfid_row_y_bottom],
    [(rfid_board_w+rfid_near_ic_span)/2, rfid_row_y_bottom]
];
// Convert PCB upper-left X-right/Y-down coordinates to enclosure XY.
function rfid_xy(p) = [rfid_x+p[0]-rfid_board_w/2, rfid_y+rfid_board_h/2-p[1]];
// Convert a PCB hole measured from its bottom-left into the enclosure XY,
// rotating around PCB centre. Rotation is applied equally to the preview.
function bpi_xy(p) = [
    bpi_x + (p[0]-bpi_board_w/2)*cos(bpi_rotation) - (p[1]-bpi_board_h/2)*sin(bpi_rotation),
    bpi_y + (p[0]-bpi_board_w/2)*sin(bpi_rotation) + (p[1]-bpi_board_h/2)*cos(bpi_rotation)
];

// SSD1309 mounting-hole spacing measured by owner (X=66.5, Y=39.5 mm).
// Assumes symmetrical placement about display PCB centre; verify PCB centre offset.
oled_corner_seat_h = 1.5; // same PCB support plane as internal M2 screw bosses
oled_fit_fudge = 0.5; // edge clearance, same at all four corners
// One common PCB/hole datum: preserves the measured +1.5mm hole shift.

oled_corner_leg_x = 5.0;
oled_corner_leg_y = 4.0;
oled_corner_leg_width = 1.1;
oled_hole_y_shift = 1.5; // preserved mounting datum shift
oled_pcb_y = oled_y + oled_hole_y_shift;
oled_solder_relief_w = 17;
oled_solder_relief_h = 2;
oled_solder_relief_depth = 2.4; // from rear/inside surface into front skin
oled_solder_relief_y = oled_pcb_y + oled_board_h/2 - oled_solder_relief_h/2;
oled_hole_span_x = 66.5; // measured centre-to-centre, October 2026
 oled_hole_span_y = 39.5; // measured centre-to-centre, October 2026
module_post_d = 5.8;
// OLED attaches from the open back using four M2 x 5mm self-tapping screws.
// Preserve the measured four mounting centres and seat the PCB on 1.5mm
// internal supports. The window and PCB XY position are unchanged.
// A typical 1.6mm PCB leaves 3.4mm of the 5mm screw in the pilot.
// The blind hole stops >=0.5mm short of the outer face.
oled_glass_protrusion_h = 2.9;
module_post_height = 0;
oled_screw_length = 5.0;
oled_pcb_thickness_nominal = 1.6;
oled_mount_boss_d = 5.8;
oled_mount_boss_h = oled_corner_seat_h;
oled_mount_pilot_d = 1.8;
oled_mount_pilot_depth = oled_screw_length - oled_pcb_thickness_nominal + 0.1;
assert(oled_mount_pilot_depth <= oled_mount_boss_h+front_skin-0.5,
       "OLED M2 pilot must not break through front face");
rfid_standoff_h = 3.5; // RC522 standoff height (unchanged)
module_pilot_d = 1.8; // M2 self tapping, matched kid-clock
buzzer_diameter = 22.6; // confirm buzzer dimensions before print
buzzer_ring_h = 5; // circular locating surround height measured from inner front face
buzzer_mount_span = 30; // measured centre-to-centre M2 spacing
bpi_y = ((buzzer_y - buzzer_mount_span/2) + (-H/2 + wall))/2; // midpoint of lower buzzer screw and inside bottom wall
buzzer_post_h = 10; // 10 mm above inside front face; both M2 posts

// USB-C breakout: board mounted VERTICALLY in XZ, socket faces DOWN (-Y).
// Front of clock is +Z; rear (wall) is -Z. No connector points toward wall.
// Board envelope and mounting pattern follow kid-clock USB-C breakout.
usb_c_panel_w = 20;       // Kid-clock USB-C PCB width
usb_c_panel_h = 7;        // Kid-clock PCB height in upright XZ plane
usb_c_panel_depth = 2.2; // Historical kid-clock pocket dimension (reference)
usb_c_pcb_thickness = 1.7; // PCB is recessed INTO the OUTSIDE bottom face
usb_c_recess_fudge = 0.25;
usb_c_hole_span = 16;
usb_c_mount_pilot_d = 1.8;
usb_c_screw_clearance_d = 1.8;
usb_c_face_recess_d = 0;
usb_c_face_fudge = 0.4;
usb_c_support_rail_w = 1.4;
usb_c_support_end_w = 1.4;
usb_c_support_rail_h = 5.0;
usb_c_header_cutout_w = 12.2;
usb_c_port_h = 7.0;
usb_c_x = 0;
usb_c_z = 8.5;
usb_c_wall_outer_y = -H/2;
usb_c_wall_inner_y = usb_c_wall_outer_y + wall;
// Board is inserted FROM OUTSIDE. Its visible face is flush at the outer wall.
// The pocket floor is 1.7mm INSIDE that outer face, NOT inside the housing.
usb_c_mount_y = usb_c_wall_outer_y + usb_c_pcb_thickness;
usb_c_block_inside = 5.0; // solid reinforcement protruding into case behind wall
usb_c_block_end_y = usb_c_wall_inner_y + usb_c_block_inside;
usb_c_pilot_depth = 5.0; // blind pilot from recessed PCB seat into solid block
usb_c_port_z = usb_c_z - usb_c_port_h/2;

module rounded2(w,h,r){
    offset(r=r) square([w-2*r,h-2*r],center=true);
}
module rr(w,h,z,r){linear_extrude(height=z) rounded2(w,h,r);}
// The wider-spaced hole row is the bottom reference edge of the 45mm antenna square.
// The square extends upwards from that row; the RC522 screw mounts do not move.
rfid_wide_hole_y = rfid_xy(rfid_holes_from_top_left[2])[1];
rfid_mark_centre_y = rfid_wide_hole_y + rfid_tap_zone_h/2;
// Engraved RFID/card symbol, centred inside the enlarged outline.
// All features are solid 2D strokes cut 0.45 mm into the exterior surface.
rfid_icon_line = 0.65;
rfid_icon_card_w = 11;
rfid_icon_card_h = 15;
module rfid_icon_arc(r,th,a0,a1){
    steps = 28;
    polygon(points=concat(
        [for(i=[0:steps]) [(r+th/2)*cos(a0+(a1-a0)*i/steps),
                              (r+th/2)*sin(a0+(a1-a0)*i/steps)]],
        [for(i=[steps:-1:0]) [(r-th/2)*cos(a0+(a1-a0)*i/steps),
                                (r-th/2)*sin(a0+(a1-a0)*i/steps)]]));
}
module rfid_icon_2d(){
    // Small contactless card on left, three outward-facing radio waves on right.
    translate([-6,0]) difference(){
        offset(r=1) square([rfid_icon_card_w-2,rfid_icon_card_h-2],center=true);
        offset(r=0.05) square([rfid_icon_card_w-2-2*rfid_icon_line,
                               rfid_icon_card_h-2-2*rfid_icon_line],center=true);
    }
    for(r=[4,7,10])
        translate([2,0]) rfid_icon_arc(r,rfid_icon_line,-55,55);
}
module rfid_mark(){
    // Recessed 57mm outline centred on nominal 45mm antenna square.
    // 6mm extension beyond each edge of that reference area.
    translate([rfid_x,rfid_mark_centre_y,D-rfid_mark_depth])
        linear_extrude(height=rfid_mark_depth+0.10)
            difference(){
                rounded2(rfid_mark_w,rfid_mark_h,rfid_mark_radius);
                rounded2(rfid_mark_w-2*rfid_mark_line,
                         rfid_mark_h-2*rfid_mark_line,
                         rfid_mark_radius-rfid_mark_line);
            }
}

// Three bottom flats and two 30-degree transitions. The 0.2mm perimeter
// allowance is added to outer silhouette without changing feature lengths.
module oled_stepped_window_cut() {
    x0 = oled_x - oled_view_w/2;
    x1 = oled_x + oled_view_w/2;
    y0 = oled_y - oled_view_h/2;
    y1 = oled_y + oled_view_h/2 + oled_opening_top_extra;
    z0 = D - front_skin - 0.1;
    depth = front_skin + flat_cap_h + 0.4;
    left_start = x0 + oled_outer_flat_w;
    left_centre = left_start + oled_slope_run;
    right_start = x1 - oled_outer_flat_w;
    right_centre = right_start - oled_slope_run;
    translate([0,0,z0])
        linear_extrude(height=depth)
            polygon(points=[
                [x0,y1], [x1,y1],
                [x1,y0+oled_view_chamfer_h],
                [right_start,y0+oled_view_chamfer_h],
                [right_centre,y0],
                [left_centre,y0],
                [left_start,y0+oled_view_chamfer_h],
                [x0,y0+oled_view_chamfer_h]
            ]);
}

// Four PCB-corner L seats, 1mm tall, following measured 71.13 x 43.57mm board.
// The same 0.5mm inward fudge is applied on every PCB edge.
// PCB corners and mounting holes share a centre datum at oled_pcb_y.
// The requested +1.5mm shift is retained for the complete PCB mounting pattern.
// Each L stays inside the PCB footprint, at the outer corner. The narrow
// strips leave the raised glass cutout and top-centre solder relief open.
module oled_l_corner_seat(sx, sy) {
    // Equal offsets from the same PCB datum used for the hole pattern.
    px = oled_x + sx*(oled_board_w/2 - oled_fit_fudge);
    py = oled_pcb_y + sy*(oled_board_h/2 - oled_fit_fudge);
    translate([px,py,D-front_skin-oled_corner_seat_h])
        union() {
            translate([sx < 0 ? 0 : -oled_corner_leg_x,
                       sy < 0 ? 0 : -oled_corner_leg_width, 0])
                cube([oled_corner_leg_x,oled_corner_leg_width,oled_corner_seat_h]);
            translate([sx < 0 ? 0 : -oled_corner_leg_width,
                       sy < 0 ? 0 : -oled_corner_leg_y, 0])
                cube([oled_corner_leg_width,oled_corner_leg_y,oled_corner_seat_h]);
        }
}

// Kid-clock-style half-moon OLED screw seats.
// The opening-side arc is relieved against the exact OLED glass silhouette.
// Screw centres, M2 pilots, and PCB support plane remain unchanged.
// The relief extends through the boss only, never through the outer face.
oled_boss_glass_clearance = 0.30;
module oled_glass_profile_2d() {
    x0 = oled_x - oled_view_w/2;
    x1 = oled_x + oled_view_w/2;
    y0 = oled_y - oled_view_h/2;
    y1 = oled_y + oled_view_h/2 + oled_opening_top_extra;
    ls = x0 + oled_outer_flat_w;
    lc = ls + oled_slope_run;
    rs = x1 - oled_outer_flat_w;
    rc = rs - oled_slope_run;
    polygon([[x0,y1],[x1,y1],[x1,y0+oled_view_chamfer_h],
             [rs,y0+oled_view_chamfer_h],[rc,y0],
             [lc,y0],[ls,y0+oled_view_chamfer_h],
             [x0,y0+oled_view_chamfer_h]]);
}
module oled_half_moon_mount(cx,cy) {
    translate([0,0,D-front_skin-oled_mount_boss_h])
        linear_extrude(height=oled_mount_boss_h+0.02)
            difference() {
                translate([cx,cy]) circle(d=oled_mount_boss_d);
                offset(delta=oled_boss_glass_clearance) oled_glass_profile_2d();
            }
}

// Face up: front outer surface at z=D; open back at z=0.
module front_shell(){
    difference(){
        union(){
            // Outer shell, relieved from the rear
            difference(){
                rr(W,H,D,corner);
                translate([0,0,-0.1]) rr(W-2*wall,H-2*wall,D-front_skin+0.1,corner-wall);
            }
            // Flat-cap disabled.
            // BPI M2 mounts on the inner face, accessible from the open back
            bpi_mounts();
            usb_c_cradle();
            // Four symmetric 0.5mm-inset L seats support PCB corners and provide 1mm
            // solder clearance; no separate hardware or L-shaped pads.
            for(x=[-1,1],y=[-1,1]) {
                oled_l_corner_seat(x,y);
                // Half-moon screw seat: opening-side material removed, screw enters inside.
                oled_half_moon_mount(
                    oled_x+x*oled_hole_span_x/2,
                    oled_pcb_y+y*oled_hole_span_y/2);
            }
            // RC522: four short M2 standoffs; antenna faces front wall
            for(p=rfid_holes_from_top_left)
                translate([rfid_xy(p)[0],rfid_xy(p)[1],
                           D-front_skin-rfid_standoff_h])
                    kid_post(rfid_standoff_h,module_post_d,2.0, rfid_xy(p)[0] < rfid_x ? 180 : 0);
            // Buzzer: two screw posts plus a separate printed retaining bridge
            for(y=[-1,1])
                translate([buzzer_x,buzzer_y+y*buzzer_mount_span/2,
                           D-front_skin-buzzer_post_h])
                    buzzer_post(buzzer_post_h,5.8,3.0,y < 0 ? 270 : 90);
            // Raised circular buzzer positioning lip, not a solid block
            translate([buzzer_x,buzzer_y,D-front_skin-buzzer_ring_h])
                difference(){
                    cylinder(h=buzzer_ring_h,d=buzzer_diameter+2.2);
                    translate([0,0,-0.1]) cylinder(h=buzzer_ring_h+0.2,d=buzzer_diameter+0.4);
                }

        }
        // Visible RFID tap-zone outline, shallow recessed engraving.
        rfid_mark();
        translate([rfid_x,rfid_mark_centre_y,D-rfid_mark_depth])
            linear_extrude(height=rfid_mark_depth+0.10) rfid_icon_2d();
        // Window
        oled_stepped_window_cut();
        // Buzzer vents
        for(x=[-1:1],y=[-1:1])
            translate([buzzer_x+x*3.5,buzzer_y+y*3.5,D-front_skin-0.1]) cylinder(h=front_skin+flat_cap_h+0.4,d=2.6);
        // Rear-face pocket for raised solder joints at top centre of OLED PCB.
        // The 1mm L seats also separate PCB solder from this face.
        // Stop 0.5mm short of outside face (2.9mm skin - 2.4mm pocket).
        translate([oled_x-oled_solder_relief_w/2,
                   oled_solder_relief_y-oled_solder_relief_h/2,
                   D-front_skin-0.01])
            cube([oled_solder_relief_w,oled_solder_relief_h,
                  oled_solder_relief_depth+0.01]);
        // Four INTERNAL blind M2 pilots, same PCB centre positions.
        // Exterior face remains unpierced. Designed for M2 x 5mm screws.
        for(x=[-1,1],y=[-1,1]) {
            translate([oled_x+x*oled_hole_span_x/2,
                       oled_pcb_y+y*oled_hole_span_y/2,
                       D-front_skin-oled_mount_boss_h-0.05])
                cylinder(h=oled_mount_pilot_depth+0.05,
                         d=oled_mount_pilot_d);
        }
        // RC522 blind M2 pilots, from open back through posts only.
        // Do not pierce the outer RFID-facing wall.
        for(p=rfid_holes_from_top_left)
            translate([rfid_xy(p)[0],rfid_xy(p)[1],
                       D-front_skin-rfid_standoff_h-0.05])
                cylinder(h=rfid_standoff_h+1.45,d=module_pilot_d);
        for(y=[-1,1])
            translate([buzzer_x,buzzer_y+y*buzzer_mount_span/2,
                       D-front_skin-buzzer_post_h-0.05])
                cylinder(h=buzzer_post_h-0.9,d=module_pilot_d);
        // Downward USB-C breakout, bottom wall exit and blind M2 supports.
        usb_c_port_opening();
        usb_c_cradle_voids();
    }
}

// USB-C breakout PCB sits in a shallow pocket in the EXTERIOR bottom wall.
// Behind that pocket is a solid internal strengthening block, tied to the
// bottom wall and growing continuously from the front face during printing.
module usb_c_cradle(){
    block_w = usb_c_panel_w + 2*usb_c_support_end_w;
    translate([usb_c_x-block_w/2,
               usb_c_wall_inner_y-0.25,
               split_back_z])
        cube([block_w,
              usb_c_block_end_y-(usb_c_wall_inner_y-0.25),
              D-front_skin-split_back_z]);
}
module usb_c_cradle_voids(){
    // EXTERIOR pocket: remove exactly the PCB thickness from the outside
    // surface inward. PCB sits flush with the case bottom, NOT inside face.
    translate([usb_c_x-(usb_c_panel_w+usb_c_recess_fudge)/2,
               usb_c_wall_outer_y-0.1,
               usb_c_z-(usb_c_panel_h+usb_c_recess_fudge)/2])
        cube([usb_c_panel_w+usb_c_recess_fudge,
              usb_c_pcb_thickness+0.1,
              usb_c_panel_h+usb_c_recess_fudge]);

    // Blind M2 pilots from EXTERIOR PCB pocket floor into solid wall/block.
    for(dx=[-usb_c_hole_span/2,usb_c_hole_span/2])
        translate([usb_c_x+dx,usb_c_mount_y-0.02,usb_c_z])
            rotate([-90,0,0])
                cylinder(h=usb_c_pilot_depth+0.02,
                         d=usb_c_screw_clearance_d);
}
module usb_c_port_opening(){
    // Connector body passage is independent of the external PCB seating pocket.
    translate([usb_c_x-usb_c_header_cutout_w/2,
               usb_c_wall_outer_y-0.2,usb_c_port_z])
        cube([usb_c_header_cutout_w,
              usb_c_block_end_y-usb_c_wall_outer_y+0.4,
              usb_c_port_h]);
}

// Kid-clock v3.47 support concept: rounded hull taper, not rectangular ribs.
// Supports extend toward the inner face and never below the open-back datum.
module kid_gusset(h, length, width, angle){
    rotate([0,0,angle]) hull(){
        cylinder(h=h, d=width, $fn=24);
        translate([length,0,h-fdm_layer_h])
            cylinder(h=fdm_layer_h,d=width,$fn=24);
    }
}
// Tail starts near post centre and points in the specified XY direction.
// For PCB mounts use angle=180 on the left and angle=0 on the right.
module kid_post(h, od, fin_length=3, angle=0){
    union(){
        cylinder(h=h,d=od);
        // Identical Z orientation to buzzer support tails.
        kid_gusset(h,od/2-0.5+fin_length,1.6,angle);
    }
}
// Buzzer gussets: full height at the standoff and tapered toward the skin
// at the outward tail. Local z=0 is the free screw end; z=h is the skin.
module buzzer_post(h, od, fin_length=3, angle=0){
    fin_w = 1.6;
    fin_run = od/2-0.5+fin_length;
    union(){
        cylinder(h=h,d=od);
        rotate([0,0,angle]) hull(){
            // Broad/high end attached to the post.
            cylinder(h=h,d=fin_w,$fn=24);
            // Small foot of the tail is attached directly to the skin.
            translate([fin_run,0,h-fdm_layer_h])
                cylinder(h=fdm_layer_h,d=fin_w,$fn=24);
        }
    }
}
module bpi_mounts(){
    // Four identical M2 self-tapping pilot posts (no locating peg).
    // Same 58 x 23 hole centres, OD 5.8, height 6, tapered rounded fins.
    base_z = D-front_skin-bpi_standoff_h;
    for(i=[0:3]) {
        hx = bpi_xy(bpi_holes[i])[0];
        hy = bpi_xy(bpi_holes[i])[1];
        // Outward from the rotated BPI centre in enclosure X.
        angle = hx < bpi_x ? 180 : 0;
        translate([hx,hy,base_z]) {
            difference(){
                union(){
                    cylinder(h=bpi_standoff_h,d=bpi_standoff_od);
                    // Wide against the front skin, taper toward the free end.
                    // The outer tail stays at skin height, not standoff-tip height.
                    translate([0,0,bpi_standoff_h-bpi_gusset_h])
                        kid_gusset(bpi_gusset_h, bpi_gusset_len,
                                   bpi_gusset_w, angle);
                }
                translate([0,0,-0.05])
                    cylinder(h=5.3,d=bpi_standoff_pilot_d);
            }
        }
    }
}
// The buzzer has no guaranteed mounting holes; screw this bridge onto
// its two M2 standoffs to hold the round housing in the recess.
module buzzer_clamp(){
    difference(){
        union(){
            hull(){
                for(y=[-buzzer_mount_span/2,buzzer_mount_span/2])
                    translate([0,y,0]) cylinder(h=2.4,d=7.5);
            }
        }
        for(y=[-buzzer_mount_span/2,buzzer_mount_span/2])
            translate([0,y,-0.1]) cylinder(h=2.6,d=2.3); // M2 clearance
    }
}
module layout(){
    color([0.75,0.75,0.75,0.35]) front_shell();
    color([0.1,0.15,0.8,0.8]) translate([oled_x,oled_pcb_y,D-front_skin-0.8])
        cube([oled_board_w,oled_board_h,1.6],center=true);
    // Glass height preview: glass extends into the window to the outer surface.
    // This is only a Z-height envelope, not a measured XY glass footprint.
    color([0.05,0.05,0.05,0.6])
        translate([oled_x,oled_y,D-front_skin])
            linear_extrude(height=oled_glass_protrusion_h)
                polygon(points=[
                    [-oled_glass_w/2, oled_glass_h/2],
                    [ oled_glass_w/2, oled_glass_h/2],
                    [ oled_glass_w/2, -oled_glass_h/2 + oled_glass_chamfer_h],
                    [ oled_glass_w/2 - oled_outer_flat_w, -oled_glass_h/2 + oled_glass_chamfer_h],
                    [ oled_glass_bottom_flat_w/2, -oled_glass_h/2],
                    [-oled_glass_bottom_flat_w/2, -oled_glass_h/2],
                    [-oled_glass_w/2 + oled_outer_flat_w, -oled_glass_h/2 + oled_glass_chamfer_h],
                    [-oled_glass_w/2, -oled_glass_h/2 + oled_glass_chamfer_h]
                ]);
    color([0,0.25,0.9,0.8]) translate([rfid_x,rfid_y,D-front_skin-rfid_standoff_h-0.8]) cube([rfid_board_w,rfid_board_h,1.6],center=true);
    color([0,0.55,0.2,0.8]) translate([bpi_x,bpi_y,D-front_skin-bpi_standoff_h-0.8]) rotate([0,0,bpi_rotation]) cube([bpi_board_w,bpi_board_h,1.6],center=true);
    color([0.85,0.5,0.10,0.9]) translate([usb_c_x,usb_c_wall_outer_y+usb_c_pcb_thickness/2,usb_c_z]) cube([usb_c_panel_w,usb_c_pcb_thickness,usb_c_panel_h],center=true);
}

// v8.8 two-piece conversion: integrated middle wall, four removable-cover screws.
// Original v8.7 electronics geometry retained; physical connector clearance must be tested.
split_back_z=3.0; // rear cover thickness; continuous front shell begins at this datum
// Keep USB-C support clear of rear seam relief.
assert(usb_c_z - (usb_c_panel_h+usb_c_face_fudge)/2 >= split_back_z + seam_overlap_h - 0.01, "USB-C face recess enters rear seam");

// One overlapping rear-cover light-baffle seam.
// Each plate has an inward-facing rim at the OUTER edge that overlaps a
// matching reduced-width section of the middle wall.
// Clearance is radial (per side), not diametral.
seam_overlap_h = 1.8;
seam_skirt_w = 1.15;
seam_clearance = 0.25;
// Middle wall has at least 1.40mm of material under the stepped joint.
assert(wall > seam_skirt_w + seam_clearance + 1.0);

module edge_skirt(h) {
    difference() {
        rr(W,H,h,corner);
        translate([0,0,-0.1])
            rr(W-2*seam_skirt_w,H-2*seam_skirt_w,h+0.2,
                corner-seam_skirt_w);
    }
}

// Remove ONLY the outside portion of the middle-wall perimeter at the ends.
// The inner web is left continuous: no through seam or light path.
module outer_seam_relief(z0) {
    translate([0,0,z0])
        difference() {
            rr(W+0.2,H+0.2,seam_overlap_h+0.02,corner+0.1);
            translate([0,0,-0.1])
                rr(W-2*(seam_skirt_w+seam_clearance),
                   H-2*(seam_skirt_w+seam_clearance),
                   seam_overlap_h+0.22,
                   corner-seam_skirt_w-seam_clearance);
        }
}


// v8.11: exact kid-clock v3.26 fastening dimensions and mirrored mount concept.
// The rear cover is at z=0..3; the kid-clock mounts are mirrored in Z so
// the broad screw-engagement end sits against that cover, tapering into the
// front shell. Corner fastener XY coordinates retain the original 7mm margin.
rear_screw_x = W/2 - 7.0;
rear_screw_y = H/2 - 7.0;
rear_screw_pass_d = 3.0;       // kid lid_screw_d
rear_head_d = 6.8;             // kid lid_screw_head_d
rear_head_recess = 1.8;        // kid lid_screw_head_recess_h
rear_screw_pilot_d = 2.65;     // kid lid_mount_pilot_d
rear_pilot_entry_d = 3.05;     // kid lid_mount_pilot_entry_d
rear_pilot_entry_h = 2.2;      // kid lid_mount_pilot_entry_h
rear_pilot_depth = 10.0;       // kid lid_mount_blind_pilot_depth
rear_mount_top_h = 3.4;        // kid lid_mount_top_h (nearest cover)
rear_mount_grow_h = 5.5;      // kid lid_mount_grow_h
rear_mount_base_reach = 1.2;   // kid lid_mount_base_reach
rear_mount_wall_overlap = 0.55;
rear_mount_inner_extra = 3.5;
rear_mount_reach_scale = 1.30;
rear_mount_hull_slice_h = 0.25;

// Support-free fastener structure in front-face-down printing orientation.
// Z=D is the build plate; the corner bosses grow into the enclosure as Z decreases.
// Start from BOTH perimeter walls near the inside front face, then grow a
// continuous 45-degree ramp to the full-diameter screw boss. At 0.24mm/layer
// inward horizontal growth stays <= 0.24mm per layer (no unsupported islands).
// At and below the pilot's blind end the boss remains full diameter.
rear_boss_d = 9.5;
rear_ramp_start_z = D-front_skin-0.2;
rear_ramp_full_z = split_back_z+rear_pilot_depth; // z=13; keep 10mm screw purchase
rear_ramp_wall_anchor_w = 1.2;
rear_ramp_slab_h = 0.28;
rear_mount_end_z = rear_ramp_start_z;
assert(rear_ramp_start_z-rear_ramp_full_z >= 3.5,
       "Corner ramp too steep for support-free face-down printing");
assert(rear_boss_d/2 > 7.0-wall+0.5,
       "Boss must overlap the inner walls");

module rear_corner_mount(sx,sy){
    cx = sx*rear_screw_x;
    cy = sy*rear_screw_y;
    // A full-size pilot sleeve nearest the rear cover, joined to both walls.
    translate([cx,cy,split_back_z])
        cylinder(d=rear_boss_d,h=rear_ramp_full_z-split_back_z+0.15);
    // At the first printed layers, the two short rails lie INSIDE the walls.
    // Their hull makes a smoothly growing corner bridge into the screw sleeve.
    // Keep anchors inside the perimeter wall's solid cross-section.
    hull(){
        translate([cx,cy,rear_ramp_full_z-0.14])
            cylinder(d=rear_boss_d,h=rear_ramp_slab_h);
        translate([sx*(W/2-wall/2),cy,rear_ramp_start_z-0.14])
            cube([wall+0.2,rear_ramp_wall_anchor_w,rear_ramp_slab_h],center=true);
        translate([cx,sy*(H/2-wall/2),rear_ramp_start_z-0.14])
            cube([rear_ramp_wall_anchor_w,wall+0.2,rear_ramp_slab_h],center=true);
    }
}
module rear_fastener_bosses(){
    for(sx=[-1,1],sy=[-1,1]) rear_corner_mount(sx,sy);
}
// Raised build identification on the inside face, beside the RC522 antenna.
// Rotated so the version reads vertically when looking into the open housing.
// Base overlaps the face slightly to ensure one printable solid.
case_version = "v8.38";
case_version_label = str("RFID ", case_version);
case_version_size = 3.6; // thicker print strokes
case_version_height = 1.2; // raised INTO the cavity, not buried in face
case_version_x = 17.5; // beside right edge of RFID PCB; clear of BPI mounting footprint
case_version_y = rfid_y;
module interior_version_mark(){
    translate([case_version_x,case_version_y,D-front_skin+0.08])
        mirror([0,0,1])
            linear_extrude(height=case_version_height+0.08)
                rotate([0,0,90])
                mirror([1,0,0]) // readable from the open rear of the enclosure
                    text(case_version_label,size=case_version_size,
                     halign="center",valign="center",font="Liberation Sans:style=Bold");
}

// Adult-clock-compatible external Wi-Fi antenna bulkhead bore.
// Right side when viewed from front, below the buzzer and its lower M2 post.
// Entirely inside the front shell side wall, not the removable back.
wifi_antenna_hole_d = 6.4;
wifi_antenna_side_y = buzzer_y - buzzer_mount_span/2 - 15; // 8 mm, 15 mm below lower mounting screw
wifi_antenna_side_z = 11.0;
assert(wifi_antenna_side_y - wifi_antenna_hole_d/2 > -H/2+corner+3,
       "Wi-Fi cutout encroaches on rounded corner");
assert(wifi_antenna_side_z - wifi_antenna_hole_d/2 > split_back_z+seam_overlap_h,
       "Wi-Fi cutout encroaches on rear cover seam");
assert(wifi_antenna_side_z + wifi_antenna_hole_d/2 < D-front_skin,
       "Wi-Fi cutout encroaches on front face");
module wifi_antenna_side_bore(){
    translate([W/2-wall-1, wifi_antenna_side_y, wifi_antenna_side_z])
        rotate([0,90,0])
            cylinder(d=wifi_antenna_hole_d, h=wall+2, $fn=48);
}

module front_plate(){
    difference(){
        union(){
            intersection(){
                front_shell();
                translate([-W,-H,split_back_z])
                    cube([2*W,2*H,D-split_back_z+0.1]);
            }
            // Limit only new corner ramps/sleeves to the exact external
            // rounded footprint. No material can extend beyond the corners.
            intersection(){
                rear_fastener_bosses();
                translate([0,0,split_back_z])
                    rr(W,H,D-split_back_z,corner);
            }
            interior_version_mark();
        }
        outer_seam_relief(split_back_z);
        if (wifi_antenna) wifi_antenna_side_bore();
        // Kid-clock stepped blind pilots: 3.05mm entry for 2.2mm, then
        // 2.65mm pilot through a total 10mm depth, leaving solid material.
        for (x=[-rear_screw_x,rear_screw_x],y=[-rear_screw_y,rear_screw_y]) {
            translate([x,y,split_back_z-0.01])
                cylinder(d=rear_screw_pilot_d,h=rear_pilot_depth+0.02);
            translate([x,y,split_back_z-0.01])
                cylinder(d=rear_pilot_entry_d,h=rear_pilot_entry_h+0.02);
        }
    }
}
// Passive ventilation behind the vertically mounted BPI.
// Slots are cut through the rear plate only. No vent touches the perimeter
// light-baffle lip; keep all positions tied to the BPI placement.
rear_vent_count = 5;
rear_vent_width = 2.6;
rear_vent_length = 43;
rear_vent_pitch = 6;
rear_vent_corner_r = 1.2;

module rear_bpi_vents(){
    for (i=[0:rear_vent_count-1]) {
        translate([bpi_x + (i-(rear_vent_count-1)/2)*rear_vent_pitch,
                   bpi_y, -0.1])
            linear_extrude(height=split_back_z+0.2)
                offset(r=rear_vent_corner_r)
                    square([rear_vent_width-2*rear_vent_corner_r,
                            rear_vent_length-2*rear_vent_corner_r],center=true);
    }
}

// Two wall screw keyholes. Mounting screws remain fixed in the wall;
// slide enclosure DOWN so the screw shanks engage the upper, narrow slots.
// Dimensioned for common pan-head wall screws; verify chosen screw heads.
wall_mount_x = [-40,40];  // 80mm horizontal screw spacing
wall_mount_y = 10;        // level mounting line, clear of BPI vents
wall_mount_head_d = 8.0;  // clearance for wall screw head
wall_mount_shank_d = 4.3; // clearance for screw shank
wall_mount_travel = 9.0;  // upwards from large entry to retained position
assert(wall_mount_head_d > wall_mount_shank_d);
assert(max(wall_mount_x)+wall_mount_head_d/2 < W/2-wall-1);
assert(wall_mount_y+wall_mount_travel+wall_mount_shank_d/2 < H/2-wall-1);
module rear_wall_keyholes(){
    for (x = wall_mount_x) {
        // Large entry is below the final screw position so the case drops
        // down onto the fixed screws. Cut is through rear cover only.
        translate([x,wall_mount_y,-0.1])
            cylinder(d=wall_mount_head_d,h=split_back_z+0.2);
        translate([x-wall_mount_shank_d/2,wall_mount_y,-0.1])
            cube([wall_mount_shank_d,wall_mount_travel,split_back_z+0.2]);
        translate([x,wall_mount_y+wall_mount_travel,-0.1])
            cylinder(d=wall_mount_shank_d,h=split_back_z+0.2);
    }
}

module rear_plate(){
    difference(){
        union(){
            rr(W,H,split_back_z,corner);
            // Rear skirt rises over the BOTTOM of the middle-wall step.
            translate([0,0,split_back_z]) edge_skirt(seam_overlap_h);
        }
        rear_bpi_vents();
        rear_wall_keyholes();
        for (x=[-rear_screw_x,rear_screw_x],y=[-rear_screw_y,rear_screw_y]) {
            translate([x,y,-0.1]) cylinder(d=rear_screw_pass_d,h=split_back_z+0.2);
            // Matching kid-clock tapered countersink, flush at the outer face
            // (external rear surface is z=0 rather than kid lid z=lid_t).
            translate([x,y,-0.1])
                cylinder(d1=rear_head_d,d2=rear_screw_pass_d,
                         h=rear_head_recess+0.1);
        }
    }
}
module assembly(){
    color([0.85,0.85,0.9,0.8]) front_plate();
    color([0.6,0.65,0.7,0.6]) rear_plate();
}

// A small print coupon using the EXACT production front-plate OLED profile.
// Includes the corner PCB seats, four mounting holes and solder relief.
// Front face is oriented on the build plate (z=0), no supports.
// Edit oled_test_border to change the surrounding plastic margin.
oled_test_border = 3.0;
oled_test_w = oled_board_w + 2*oled_test_border;
oled_test_h = oled_board_h + 2*oled_test_border;
oled_test_z0 = D-front_skin-oled_corner_seat_h-0.15;

module oled_fit_test() {
    // Crop the real production geometry, not a separately drawn cutout.
    // Turn face down to lie flat on the print bed at z=0.
    translate([0,0,D])
        rotate([180,0,0])
            intersection() {
                front_plate();
                translate([oled_x-oled_test_w/2,
                           oled_pcb_y-oled_test_h/2,
                           oled_test_z0])
                    cube([oled_test_w,oled_test_h,D-oled_test_z0+0.10]);
            }
}

if(part=="front") front_plate();
else if(part=="oled_fit_test") oled_fit_test();
else if(part=="back") rear_plate();
else if(part=="buzzer_clamp") buzzer_clamp();
else if(part=="layout") layout();
else assembly();
