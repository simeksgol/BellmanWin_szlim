#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <assert.h>
#include "universe.h"
#include "readwrite.h"
#include "bitwise.h"

#define PRINT_DIAGNOSTIC_RECURSE(x) //printf x
#define PRINT_DIAGNOSTIC_PRUNE(x) //printf x
#define PRINT_DIAGNOSTIC(x) //printf x
#define YES 1
#define NO 0

static universe *u_static, *u_evolving, *u_forbidden, *u_filter;


// Search settings:
#define DEFAULT_MIN_EXTRA_GENS_TO_ALLOW_REACTIVATION 12
#define MAX_MAX_LOCAL_AREAS 16
#define LOCAL_COMPLEXITY_FREE_CELLS 4
#define GLOBAL_COMPLEXITY_FREE_CELLS 9

static int explicit_max_reactivation_gen = NO;

static unsigned int min_activation_gen = 2;
static unsigned int max_first_activation_gen = 17;
static unsigned int max_reactivation_gen = 17 + DEFAULT_MIN_EXTRA_GENS_TO_ALLOW_REACTIVATION;
static unsigned int max_active_gens_in_a_row = 12;
static unsigned int inactive_gens_at_accept	= 6;
static unsigned int active_plus_inactive_gens_at_accept = 0;
static unsigned int continue_after_accept = 0;
static unsigned int max_added_static_oncells = 32;
static unsigned int max_flipped_cells_in_activation = 8;
static unsigned int max_local_complexity = 1023;
static unsigned int max_local_areas = 1;
static unsigned int min_local_area_separation_squared = 10;
static unsigned int max_global_complexity = 1023;
static unsigned int old_result_naming = 0;


// Symmetry constraints
static enum {
        NONE, HORIZ, VERT, DIAG, DIAG_INVERSE
} symmetry_type = NONE;

static unsigned int symmetry_ofs = 0;

static unsigned int diagonal_x, diagonal_y;
static unsigned int inverse_x, inverse_y;


// Other global values
static int dumpcount = 0;
static int solcount = 0;
static unsigned int max_gens;


// List of currently added static on-cells
#define ONLIST_SIZE 1024
static int onlist_x [ONLIST_SIZE];
static int onlist_y [ONLIST_SIZE];
static int onlist_cnt = 0;


// Status update values and prune counters
static int last_print_time = 0;
static int total_time = 0;
static uint64_t prune_oldtotal = 0;

static uint64_t prune_unstable = 0;
static uint64_t prune_forbidden = 0;
static uint64_t prune_filter = 0;
static uint64_t prune_solution = 0;
static uint64_t prune_no_continuation_found = 0;
static uint64_t prune_first_activation_too_early = 0;
static uint64_t prune_first_activation_too_late = 0;
static uint64_t prune_reactivation_too_late = 0;
static uint64_t prune_stayed_active_too_long = 0;
static uint64_t prune_too_many_added_oncells = 0;
static uint64_t prune_too_many_flipped_cells = 0;
static uint64_t prune_too_complex_locally = 0;
static uint64_t prune_too_complex_globally = 0;
static uint64_t prune_new_oncells_not_allowed = 0;


static int lowest_of (int arg1, int arg2)
{
	if (arg2 < arg1)
		return arg2;
	else
		return arg1;
}

static int highest_of (int arg1, int arg2)
{
	if (arg2 > arg1)
		return arg2;
	else
		return arg1;
}


#define PRINT_PRUNE(fmt, count) do {                            \
                printf("    " fmt ": %lld\n", (long long)count); \
                prune_total += count;                           \
        } while(0)

static void print_prune_counters(int force) {

//    int stacksz = lowest_of (onlist_cnt, 8);
//    
//    printf ("First few added on-cells: ");
//    int i;
//    for (i = 0; i < stacksz; i++)
//    {
//      if (i > 0)
//        printf (" ");
//      printf ("%2d-%2d", onlist_x [i], onlist_y [i]);
//    }
//    printf ("\n");
      
		if (total_time || force)
		{
	        uint64_t prune_total = 0;
			
	        printf("  Reasons why search space was pruned:\n");
    	    PRINT_PRUNE("Catalyst is unstable", prune_unstable);
        	PRINT_PRUNE("Hit forbidden region", prune_forbidden);
	        PRINT_PRUNE("Filter mismatch", prune_filter);
			if (continue_after_accept)
				PRINT_PRUNE("No continuation found", prune_no_continuation_found);
			else
	    	    PRINT_PRUNE("Found a solution", prune_solution);
	        PRINT_PRUNE("First activation too early", prune_first_activation_too_early);
    	    PRINT_PRUNE("First activation too late", prune_first_activation_too_late);
        	PRINT_PRUNE("Reactivated too late", prune_reactivation_too_late);
			PRINT_PRUNE("Stayed active too long", prune_stayed_active_too_long);
    	    PRINT_PRUNE("Too many added on-cells", prune_too_many_added_oncells);
        	PRINT_PRUNE("Too many flipped cells in activation", prune_too_many_flipped_cells);
	        PRINT_PRUNE("Too complex locally", prune_too_complex_locally);
    	    PRINT_PRUNE("Too complex globally", prune_too_complex_globally);
    	    PRINT_PRUNE("New on-cells not allowed", prune_new_oncells_not_allowed);
			
	        double prune_rate = prune_total - prune_oldtotal;
    	    prune_oldtotal = prune_total;
			
	        prune_rate = prune_rate / 10000.0;
			
	        printf("  Solutions: %d, prunes: %lld\n", solcount, (long long) prune_total);
			if (total_time)
				printf ("  Average: %.3f Kprunes/s, current: = %.3f Kprunes/s\n", (double) prune_total / (double) total_time / 10000.0, prune_rate);
		}
}



static void read_cb(void *u_, char area, int gen, int x, int y, char c) {
        cellvalue vs = OFF, ve = OFF, vf = OFF;

        (void)u_;

        if((area == 'P') && (gen == 0)) {
                switch(c) {
                case '.': break;
                case '*': vs = ve = ON; break;
                case '@': ve = ON; break;
                case '?': vs = ve = UNKNOWN_STABLE; break;

// Test to have forbidden as a type of off-cell instead
//                case '!': vs = ve = UNKNOWN_STABLE; vf = ON; break;
                case '!': vf = ON; break;
                }

                generation_set_cell(u_static->first, x, y, vs);
                generation_set_cell(u_evolving->first, x, y, ve);        
                generation_set_cell(u_forbidden->first, x, y, vf);
        } else if(area == 'F') {
                generation *g = universe_find_generation(u_filter, gen, 1);
				
				switch(c)
				{
				   case '*' :
					  generation_set_cell(g, x, y, ON);
					  break;
				   case '.' :
				   case ' ' :
					  generation_set_cell(g, x, y, OFF);
					  break;
				}
        }
}

static int match_parameter (const char *match, const char *param, const char *valuein, int addtovalue, unsigned int minvalue, unsigned int maxvalue, unsigned int *valueout)
{
	if (strcmp (match, param) != 0)
		return NO;

	unsigned int value = strtoul (valuein, NULL, 10);
	if (value < minvalue || value > maxvalue)
	{
		fprintf (stderr, "Legal range for parameter '%s' is %d to %d\n", match, minvalue, maxvalue);
		exit (-1);
	}
	
	*valueout = value + addtovalue;
	return YES;
}

static void read_param_cb(void *u_, const char *param, const char *value) {
        (void)u_;
        int match = NO;
		unsigned int coord;
		
		// Backward compatibility with traditional parameters
		// Value of repair-interval and stable-interval is incremented by one for the new definition of max-active-gens-in-a-row and inactive-gens-at-accept
        match |= match_parameter ("first-encounter", param, value, 0, 0, 1023, &min_activation_gen);
        match |= match_parameter ("last-encounter", param, value, 0, 0, 1023, &max_first_activation_gen);
        match |= match_parameter ("repair-interval", param, value, 1, 1 - 1, 1023 - 1, &max_active_gens_in_a_row);
        match |= match_parameter ("stable-interval", param, value, 1, 1 - 1, 1023 - 1, &inactive_gens_at_accept);
        match |= match_parameter ("max-live", param, value, 1, 0, 1023, &max_added_static_oncells);
        match |= match_parameter ("max-active", param, value, 1, 0, 1023, &max_flipped_cells_in_activation);
		
		// New style parameters
        match |= match_parameter ("min-activation-gen", param, value, 0, 0, 1023, &min_activation_gen);
        match |= match_parameter ("max-first-activation-gen", param, value, 0, 0, 1023, &max_first_activation_gen);
        if (match_parameter ("max-reactivation-gen", param, value, 0, 0, 1023, &max_reactivation_gen))
		{
			match = YES;
			explicit_max_reactivation_gen = YES;
		}
        match |= match_parameter ("max-active-gens-in-a-row", param, value, 0, 1, 1023, &max_active_gens_in_a_row);
        match |= match_parameter ("inactive-gens-at-accept", param, value, 0, 1, 1023, &inactive_gens_at_accept);
        match |= match_parameter ("active-plus-inactive-gens-at-accept", param, value, 0, 0, 1023, &active_plus_inactive_gens_at_accept);
        match |= match_parameter ("continue-after-accept", param, value, 0, 0, 1, &continue_after_accept);
        match |= match_parameter ("max-added-static-oncells", param, value, 0, 0, 1023, &max_added_static_oncells);
        match |= match_parameter ("max-flipped-cells-in-activation", param, value, 0, 0, 1023, &max_flipped_cells_in_activation);
        match |= match_parameter ("max-local-complexity", param, value, 0, 0, 1023, &max_local_complexity);
        match |= match_parameter ("max-local-areas", param, value, 0, 1, MAX_MAX_LOCAL_AREAS, &max_local_areas);
        match |= match_parameter ("min-local-area-separation-squared", param, value, 0, 0, 8191, &min_local_area_separation_squared);
        match |= match_parameter ("max-global-complexity", param, value, 0, 0, 1023, &max_global_complexity);
        match |= match_parameter ("old-result-naming", param, value, 0, 0, 1, &old_result_naming);
		
        if(!strcmp(param, "symmetry-horiz-odd")) {
                 coord = strtoul(value, NULL, 10);
				 symmetry_type = HORIZ;
                 symmetry_ofs = (coord * 2);

				}
        else if(!strcmp(param, "symmetry-horiz-even")) {
                 coord = strtoul(value, NULL, 10);
				 symmetry_type = HORIZ;
                 symmetry_ofs = (coord * 2) + 1;
				}       
		else if(!strcmp(param, "symmetry-vert-odd")) {
                 coord = strtoul(value, NULL, 10);
				 symmetry_type = VERT;
                 symmetry_ofs = (coord * 2);
				}       
				
        else if(!strcmp(param, "symmetry-vert-even")) {
                 coord = strtoul(value, NULL, 10);
				 symmetry_type = VERT;
                 symmetry_ofs = (coord * 2) + 1;
             }
		 else if(!strcmp(param, "symmetry-diag")) {
				fprintf (stderr, "Symmetry type 'symmetry-diag' is not implemented!\n");
				exit (-1);
				
//				if(sscanf(value, "%d %d", &diagonal_x, &diagonal_y) != 2) {
//                      fprintf(stderr, "Bad symmetry parameter: '%s'\n", value);
//                        exit(-1);
//				}
//				
//				 symmetry_type = DIAG;
             }		

         else if(!strcmp(param, "symmetry-diag-inverse")) {
				fprintf (stderr, "Symmetry type 'symmetry-diag-inverse' is not implemented!\n");
				exit (-1);
				
//				if(sscanf(value, "%d %d", &inverse_x, &inverse_y) != 2) {
//                      fprintf(stderr, "Bad symmetry parameter: '%s'\n", value);
//                        exit(-1);
//				}
//				
//				 symmetry_type = DIAG_INVERSE;
             }		

        else if (!match)
		{
			fprintf(stderr, "Unknown parameter: '%s'\n", param);
			exit (-1);
		}
}


static evolve_result bellman_evolve(tile *t, tile *out) {

        // Our evolution function is based on the 3 state Life variant.
        out->flags = tile_evolve_bitwise_3state(t, out) | CHANGED;

        // But we do another pass to (a) stop the UNKNOWN_STABLE area
        // from growing and (b) check for boundary condition
        // violations.

        tile *stable = (tile *)t->auxdata;
        if(!stable) return out->flags;
        tile *forbidden = (tile *)stable->auxdata;
        tile *filter = t->filter;
        tile *prev = t->prev;

        int y;

        TILE_WORD ul_bit0, u_bit0, ur_bit0;
        TILE_WORD ul_bit1, u_bit1, ur_bit1;
        TILE_WORD ul_bit0s, u_bit0s, ur_bit0s;
        TILE_WORD ul_bit1s, u_bit1s, ur_bit1s;       

        tile *t_up = t->up;


        if(t_up) {
                GET3WORDS(ul_bit0, u_bit0, ur_bit0, t_up, 0, TILE_HEIGHT-1);
                GET3WORDS(ul_bit1, u_bit1, ur_bit1, t_up, 1, TILE_HEIGHT-1);
        } else {
                ul_bit0 = u_bit0 = ur_bit0 = 0;
                ul_bit1 = u_bit1 = ur_bit1 = 0;
        }

        t_up = stable->up;
        if(t_up) {
                GET3WORDS(ul_bit0s, u_bit0s, ur_bit0s, t_up, 0, TILE_HEIGHT-1);
                GET3WORDS(ul_bit1s, u_bit1s, ur_bit1s, t_up, 1, TILE_HEIGHT-1);
                
        } else {
                ul_bit0s = u_bit0s = ur_bit0s = 0;
                ul_bit1s = u_bit1s = ur_bit1s = 0;
        }


        TILE_WORD l_bit0, bit0, r_bit0;
        TILE_WORD l_bit1, bit1, r_bit1;
        TILE_WORD l_bit0s, bit0s, r_bit0s;
        TILE_WORD l_bit1s, bit1s, r_bit1s;

        GET3WORDS(l_bit0, bit0, r_bit0, t, 0, 0);
        GET3WORDS(l_bit1, bit1, r_bit1, t, 1, 0);
        GET3WORDS(l_bit0s, bit0s, r_bit0s, stable, 0, 0);
        GET3WORDS(l_bit1s, bit1s, r_bit1s, stable, 1, 0);

        TILE_WORD dl_bit0, d_bit0, dr_bit0;
        TILE_WORD dl_bit1, d_bit1, dr_bit1;
        TILE_WORD dl_bit0s, d_bit0s, dr_bit0s;
        TILE_WORD dl_bit1s, d_bit1s, dr_bit1s;
		TILE_WORD all_non_active = 0; 
		
        TILE_WORD interaction = 0, activity = 0, unk_succ = 0, delta_from_stable_count = 0;
        TILE_WORD delta_from_previous_count = 0;
		TILE_WORD has_ON_cells = 0;
		
        TILE_WORD forbid = 0;
        TILE_WORD activity2 = 0, live = 0;
        TILE_WORD filter_diff_all = 0;

        for(y=0; y<TILE_HEIGHT; y++) {
                if(y == TILE_HEIGHT-1) {
                        if(t->down) {
                                GET3WORDS(dl_bit0, d_bit0, dr_bit0, t->down, 0, 0);
                                GET3WORDS(dl_bit1, d_bit1, dr_bit1, t->down, 1, 0);
                        } else {
                                dl_bit0 = d_bit0 = dr_bit0 = 0;
                                dl_bit1 = d_bit1 = dr_bit1 = 0;
                        }
                        if(stable->down) {
                                GET3WORDS(dl_bit0s, d_bit0s, dr_bit0s, stable->down, 0, 0);
                                GET3WORDS(dl_bit1s, d_bit1s, dr_bit1s, stable->down, 1, 0);
                        } else {
                                dl_bit0s = d_bit0s = dr_bit0s = 0;
                                dl_bit1s = d_bit1s = dr_bit1s = 0;
                        }
                } else {
                        GET3WORDS(dl_bit0, d_bit0, dr_bit0, t, 0, y+1);
                        GET3WORDS(dl_bit1, d_bit1, dr_bit1, t, 1, y+1);
                        GET3WORDS(dl_bit0s, d_bit0s, dr_bit0s, stable, 0, y+1);
                        GET3WORDS(dl_bit1s, d_bit1s, dr_bit1s, stable, 1, y+1);
                }
				
				//active is either 1 or unknown. (optimization)
				/*
				all_non_active |= (((ul_bit0) & (~ul_bit1)) | ((~ul_bit0) & (ul_bit1)));
				all_non_active |= (((ur_bit0) & (~ur_bit1)) | ((~ur_bit0) & (ur_bit1)));
				all_non_active |= (((u_bit0) & (~u_bit1)) | ((~u_bit0) & (u_bit1)));
				
				if(all_non_active == 0)
				{
					all_non_active |= (((dl_bit0) & (~dl_bit1)) | ((~dl_bit0) & (dl_bit1)));
					all_non_active |= (((dr_bit0) & (~dr_bit1)) | ((~dr_bit0) & (dr_bit1)));
					all_non_active |= (((d_bit0) & (~d_bit1)) | ((~d_bit0) & (d_bit1)));
				}
				
				if(all_non_active == 0)
				{
					all_non_active |= (((l_bit0) & (~l_bit1)) | ((~l_bit0) & (l_bit1)));
					all_non_active |= (((r_bit0) & (~r_bit1)) | ((~r_bit0) & (r_bit1)));
					all_non_active |= (((bit0) & (~bit1)) | ((~bit0) & (bit1)));
				}
				*/
			if(all_non_active == 0)
			{
                // Any neighbourhood which is identical to the stable
                // universe should remain stable.

                TILE_WORD stable_diff_above = 0;
                stable_diff_above |= (ul_bit0s ^ ul_bit0);
                stable_diff_above |= (ul_bit1s ^ ul_bit1);
                stable_diff_above |= (u_bit0s ^ u_bit0);
                stable_diff_above |= (u_bit1s ^ u_bit1);
                stable_diff_above |= (ur_bit0s ^ ur_bit0);
                stable_diff_above |= (ur_bit1s ^ ur_bit1);

                TILE_WORD stable_diff_mid = 0;
                stable_diff_mid |= (l_bit0s ^ l_bit0);
                stable_diff_mid |= (l_bit1s ^ l_bit1);
                stable_diff_mid |= (bit0s ^ bit0);
                stable_diff_mid |= (bit1s ^ bit1);
                stable_diff_mid |= (r_bit0s ^ r_bit0);
                stable_diff_mid |= (r_bit1s ^ r_bit1);

                TILE_WORD stable_diff_below = 0;
                stable_diff_below |= (dl_bit0s ^ dl_bit0);
                stable_diff_below |= (dl_bit1s ^ dl_bit1);
                stable_diff_below |= (d_bit0s ^ d_bit0);
                stable_diff_below |= (d_bit1s ^ d_bit1);
                stable_diff_below |= (dr_bit0s ^ dr_bit0);
                stable_diff_below |= (dr_bit1s ^ dr_bit1);

                TILE_WORD diff_mask = stable_diff_above | stable_diff_mid | stable_diff_below;

                out->bit0[y] = (out->bit0[y] & diff_mask) | (stable->bit0[y] & ~diff_mask);
                out->bit1[y] = (out->bit1[y] & diff_mask) | (stable->bit1[y] & ~diff_mask);

                // Generate a mask representing anything that's set in
                // the stable region.
                TILE_WORD stable_set_above = 0;
                stable_set_above |= (ul_bit0s & ~ul_bit1s);
                stable_set_above |= (u_bit0s & ~u_bit1s);
                stable_set_above |= (ur_bit0s & ~ur_bit1s);

                TILE_WORD stable_set_mid = 0;
                stable_set_mid |= (l_bit0s & ~l_bit1s);
                stable_set_mid |= (bit0s & ~bit1s);
                stable_set_mid |= (r_bit0s & ~r_bit1s);

                TILE_WORD stable_set_below = 0;
                stable_set_below |= (dl_bit0s & ~dl_bit1s);
                stable_set_below |= (d_bit0s & ~d_bit1s);
                stable_set_below |= (dr_bit0s & ~dr_bit1s);

                TILE_WORD set_mask = stable_set_above | stable_set_mid | stable_set_below;

                // Look for places where the output differs from the
                // stable input
                TILE_WORD was0now1 = (~bit0s & ~bit1s) & (out->bit0[y] & ~out->bit1[y]);
                TILE_WORD was1now0 = (bit0s & ~bit1s) & (~out->bit0[y] & ~out->bit1[y]);

                TILE_WORD delta_from_stable = was0now1 | was1now0;

                live |= delta_from_stable;
                delta_from_stable &= set_mask;
                interaction |= delta_from_stable;

                // Have any forbidden cells changed?
                if(forbidden)
                        forbid |= forbidden->bit0[y] & (was0now1 | was1now0);

                // Also count the number of cells which differ from
                // the stable input. 4 rounds of the bitwise bit
                // counting algorithm gets us to 16 bit subtotals
                // which we accumulate; we finish off the addition
                // outside the loop.

                // With a careful choice of tile size it should be
                // possible to move the last round out of the loop
                // too.

                delta_from_stable = (delta_from_stable & 0x5555555555555555) + ((delta_from_stable >> 1) & 0x5555555555555555);
                delta_from_stable = (delta_from_stable & 0x3333333333333333) + ((delta_from_stable >> 2) & 0x3333333333333333);
                delta_from_stable = (delta_from_stable & 0x0f0f0f0f0f0f0f0f) + ((delta_from_stable >> 4) & 0x0f0f0f0f0f0f0f0f);
                delta_from_stable = (delta_from_stable & 0x00ff00ff00ff00ff) + ((delta_from_stable >> 8) & 0x00ff00ff00ff00ff);

                delta_from_stable_count += delta_from_stable;

                // Look for places where the universe is changing
                was0now1 = (~bit0 & ~bit1) & (out->bit0[y] & ~out->bit1[y]);
                was1now0 = (bit0 & ~bit1) & (~out->bit0[y] & ~out->bit1[y]);
                TILE_WORD delta_from_previous = (was0now1 | was1now0);

                activity |= delta_from_previous;

                delta_from_previous &= set_mask;

                delta_from_previous = (delta_from_previous & 0x5555555555555555) + ((delta_from_previous >> 1) & 0x5555555555555555);
                delta_from_previous = (delta_from_previous & 0x3333333333333333) + ((delta_from_previous >> 2) & 0x3333333333333333);
                delta_from_previous = (delta_from_previous & 0x0f0f0f0f0f0f0f0f) + ((delta_from_previous >> 4) & 0x0f0f0f0f0f0f0f0f);
                delta_from_previous = (delta_from_previous & 0x00ff00ff00ff00ff) + ((delta_from_previous >> 8) & 0x00ff00ff00ff00ff);

                delta_from_previous_count += delta_from_previous;

                if(prev) {
                        was0now1 = (~prev->bit0[y] & ~prev->bit1[y]) & (out->bit0[y] & ~out->bit1[y]);
                        was1now0 = (prev->bit0[y] & ~prev->bit1[y]) & (~out->bit0[y] & ~out->bit1[y]);
                        TILE_WORD delta_from_2prev = (was0now1 | was1now0);

                        activity2 |= delta_from_2prev;

                }

                // Look for unknown successors
                unk_succ |= (out->bit1[y] & ~out->bit0[y]);
				
				//Update has on cells flag. 
				has_ON_cells |= (~out->bit1[y] & out->bit0[y]);
				
                // Compare against user-specified filter pattern
                TILE_WORD filter_bit0 = filter ? filter->bit0[y] : 0;
                TILE_WORD filter_bit1 = filter ? filter->bit1[y] : (TILE_WORD)~0;

                TILE_WORD filter_diff = out->bit0[y] ^ filter_bit0;
                filter_diff &= ~(filter_bit1 | out->bit1[y]);
                filter_diff_all |= filter_diff;
#if 0
//                if(filter_bit1 != ~0) {
//                        printf("f%d: %16llx/%16llx\n", y, filter_bit0 & ~filter_bit1, filter_bit1);
//                        printf("o%d: %16llx/%16llx\n", y, out->bit0[y] & ~filter_bit1, out->bit1[y] & ~filter_bit1);
//                        printf("d%d: %16llx\n", y, filter_diff);
//                }
#endif
#if 0
//                int x;
//                for(x=0; x<TILE_WIDTH; x++) {
//                        int cb0 = (neigh_total0 >> x) & 1;
//                        int cb1 = (neigh_total1 >> x) & 1;
//                        int cb2 = (neigh_total2 >> x) & 1;
//                        int cb3 = (neigh_total3 >> x) & 1;
//                        int ub0 = (neigh_unk_total0 >> x) & 1;
//                        int ub1 = (neigh_unk_total1 >> x) & 1;
//                        int ub2 = (neigh_unk_total2 >> x) & 1;
//                        int ub3 = (neigh_unk_total3 >> x) & 1;
//                        int v = (mid >> x) & 1;
//                        v += ((mid_unk >> x) & 1) << 1;
//                        int nv = (is_live >> x) & 1;
//                        nv += ((is_unk >> x) & 1) << 1;
//                        printf("%d, %d: v=%d, count=%d, unk=%d, new=%d, abort %x\n",
//                               x, y, v, (cb3 * 8) + (cb2 * 4) + (cb1 * 2) + cb0,
//                               (ub3 * 8) + (ub2 * 4) + (ub1 * 2) + ub0, nv, abort);
//
//                }
#endif
			}
			else
			{
				//if all activity is stable - remain the same (optimization)
				out->bit0[y] = t->bit0[y];
                out->bit1[y] = t->bit1[y];
			}
			// Shift the previous results
			ul_bit0 = l_bit0; u_bit0 = bit0; ur_bit0 = r_bit0;
			ul_bit1 = l_bit1; u_bit1 = bit1; ur_bit1 = r_bit1;

			l_bit0 = dl_bit0; bit0 = d_bit0; r_bit0 = dr_bit0;
			l_bit1 = dl_bit1; bit1 = d_bit1; r_bit1 = dr_bit1;

			ul_bit0s = l_bit0s; u_bit0s = bit0s; ur_bit0s = r_bit0s;
			ul_bit1s = l_bit1s; u_bit1s = bit1s; ur_bit1s = r_bit1s;

			l_bit0s = dl_bit0s; bit0s = d_bit0s; r_bit0s = dr_bit0s;
			l_bit1s = dl_bit1s; bit1s = d_bit1s; r_bit1s = dr_bit1s;

        }

        // The delta_from_stable and delta_from_previous counters are
        // still split into 16 bit subtotals; finish them off here

        delta_from_stable_count = (delta_from_stable_count & 0x0000ffff0000ffff) + ((delta_from_stable_count >> 16) & 0x0000ffff0000ffff);
        delta_from_stable_count = (delta_from_stable_count & 0x00000000ffffffff) + ((delta_from_stable_count >> 32) & 0x00000000ffffffff);

        delta_from_previous_count = (delta_from_previous_count & 0x0000ffff0000ffff) + ((delta_from_previous_count >> 16) & 0x0000ffff0000ffff);
        delta_from_previous_count = (delta_from_previous_count & 0x00000000ffffffff) + ((delta_from_previous_count >> 32) & 0x00000000ffffffff);

        out->n_active = delta_from_stable_count;
        out->delta_prev = delta_from_previous_count;

        if(interaction != 0) out->flags |= DIFFERS_FROM_STABLE;
        if(unk_succ != 0) out->flags |= HAS_UNKNOWN_CELLS;
		if(has_ON_cells != 0) out->flags |= HAS_ON_CELLS;
        if(forbid != 0) out->flags |= IN_FORBIDDEN_REGION;
        if(activity != 0) out->flags |= DIFFERS_FROM_PREVIOUS;
        if((activity2 != 0) || !prev) out->flags |= DIFFERS_FROM_2PREV;
        if(live != 0) out->flags |= IS_LIVE;
        if(filter_diff_all != 0) out->flags |= FILTER_MISMATCH;

        return out->flags;
}

static generation *bellman_evolve_generations(generation *g, unsigned int end) {
        tile *t;
        g->flags |= CHANGED;

        for(t = g->all_first; t; t = t->all_next)
                t->flags |= CHANGED;

        while(g->gen < end) {
                //printf("Evolving: %d\n", g->gen);
                generation_evolve(g, bellman_evolve);
                g = g->next;
        }
        return g->prev;
}

static void dump(int full, unsigned int gen_nr) {

        char name[30];
        unsigned int i;

        printf("Dumping %d\n", dumpcount);

        if(full) {
                for(i=0; i<max_gens; i++) {
                        printf("   %03d: %s\n", i, 
                               flag2str(universe_find_generation(u_evolving, i, 0)->flags));
                        
                }
        }
        dumpcount++;
}

static void bellman_found_solution(unsigned int gens) {
        solcount++;
        printf("--- Found solution %d, accepted at gen %d ---\n", solcount, gens);

        char name[30];

        unsigned int i;
		tile *t;
		
		if (old_result_naming)
	        snprintf(name, sizeof name, "result%06d-4.out", solcount);
		else
	        snprintf(name, sizeof name, "result%06d.out", solcount);
		
        FILE *f = fopen(name, "w");
        if(f) {

				fprintf(f, "#S min-activation-gen %d\n", min_activation_gen);
				fprintf(f, "#S max-first-activation-gen %d\n", max_first_activation_gen);
				fprintf(f, "#S max-reactivation-gen %d\n", max_reactivation_gen);
				fprintf(f, "#S max-active-gens-in-a-row %d\n", max_active_gens_in_a_row);
				fprintf(f, "#S inactive-gens-at-accept %d\n", inactive_gens_at_accept);
				fprintf(f, "#S active-plus-inactive-gens-at-accept %d\n", active_plus_inactive_gens_at_accept);
				fprintf(f, "#S continue-after-accept %d\n", continue_after_accept);
				fprintf(f, "#S max-added-static-oncells %d\n", max_added_static_oncells);
				fprintf(f, "#S max-flipped-cells-in-activation %d\n", max_flipped_cells_in_activation);
				fprintf(f, "#S max-local-complexity %d\n", max_local_complexity);
				fprintf(f, "#S max-local-areas %d\n", max_local_areas);
				fprintf(f, "#S min-local-area-separation-squared %d\n", min_local_area_separation_squared);
				fprintf(f, "#S max-global-complexity %d\n", max_global_complexity);
				fprintf(f, "#C Solution accepted at generation %d\n", gens);

                for(t = u_static->first->all_first; t; t = t->all_next) {
                        tile *t2 = universe_find_tile(u_evolving, 0, t->xpos, t->ypos, 0);
                        fprintf(f, "#P %d %d\n", t->xpos, t->ypos);

                        int x, y;

                        for(y=0; y<TILE_HEIGHT; y++) {
                                for(x=0; x<TILE_WIDTH; x++) {
                                        char c = '.';
                                        
                                        if(t2 && tile_get_cell(t2, x, y) == ON)
                                                c = '@';

                                        if(tile_get_cell(t, x, y) == ON)
                                                c = '*';
                                        else if(tile_get_cell(t, x, y) != OFF)
                                                c = '?';

                                        fputc(c, f);
                                }
                                fputc('\n', f);
                        }
                }


                fclose(f);
        } else perror(name);
}

static int verify_static_is_stable ()
{
	// TODO: check only the neighbourhood of the cell we just modified!

 	tile *t;
	for(t = u_static->first->all_first; t; t = t->all_next)
	{
		evolve_result res = tile_stabilise_3state (t, t->next);
        if (res & ABORT)
			return NO;
	}
	
	return YES;
}

// Forward declaration to allow mutual calls between bellman_choose_cells and bellman_recurse
static void bellman_choose_cells (universe *u, generation *g, int allow_new_oncells, int first_gen_with_unknown_cells, int first_next_sol_gen);

static void bellman_recurse (universe *u, generation *g, int previous_first_gen_with_unknown_cells, int first_next_sol_gen)
{

		int t_now = time(NULL);
        if((t_now - last_print_time) >= 10) {
		        last_print_time = t_now;
                print_prune_counters(NO);
				total_time++;
				
				if(total_time % 6 == 0)
					printf("Total time %d min \n", total_time / 6);
        }

        // First make sure the static pattern is truly static
		// Note that both a new static on-cell and a new static off-cell can cause the static pattern to become impossible to make stable
		if (!verify_static_is_stable ())
		{
			prune_unstable++;
           	PRINT_DIAGNOSTIC_PRUNE(("Stable world is unstable\n"));
			return;
		}

        // Now check that the evolving universe is behaving itself
        generation *ge;
        evolve_result all_gens = 0;

        int first_active_gen = -1;
		int stabilized = NO; 
		int stabilized_gen = -1;
		int stabilization_yielded = NO;
		
		
		// First check that we got at least one generation further than in the previous recursion level
        for(ge = u->first; ge && ge->next; ge = ge->next)
		{
        	if(ge->flags & CHANGED)
			{
            	ge->flags &= ~CHANGED;
				PRINT_DIAGNOSTIC(("Evolving generation %d\n", ge->next->gen));
				generation_evolve(ge, bellman_evolve);
			}
			
			if (ge->gen == previous_first_gen_with_unknown_cells)
				break;
		}
		
		if (!(ge->flags & HAS_UNKNOWN_CELLS))
	        for(ge = u->first; ge && ge->next; ge = ge->next)
			{
                if(ge->flags & CHANGED)
				{
                	ge->flags &= ~CHANGED;
					PRINT_DIAGNOSTIC(("Evolving generation %d\n", ge->next->gen));
					generation_evolve(ge, bellman_evolve);
                }
				
//				write_life105(stdout, ge);
//				getchar();
						
                all_gens |= ge->flags;
				
				// We handle only generations without unknown cells, this gives more prunes, but seems faster nonetheless
				if(ge->flags & HAS_UNKNOWN_CELLS)
					break;
				
				// Test for too much activity first, for performance
                if(ge->n_active > max_flipped_cells_in_activation) {
                        PRINT_DIAGNOSTIC_PRUNE(("Too much activity at generation %d\n", ge->gen));
                        prune_too_many_flipped_cells++;
                        return;
                }

				// Check if first activation should have happened by now
				if (first_active_gen == -1 && ge->gen > max_first_activation_gen)
				{
					PRINT_DIAGNOSTIC_PRUNE(("No activity before generation %d\n", last_encounter_gen));
					prune_first_activation_too_late++;
					return;                        
				}
				
				// Check for first activation
				// Verify that it doesn't happen too early
                if (first_active_gen == -1 && (ge->flags & DIFFERS_FROM_STABLE))
	                if (ge->gen < min_activation_gen)
					{
						PRINT_DIAGNOSTIC_PRUNE(("Activity before generation %d\n", min_activation_gen));
						prune_first_activation_too_early++;
						return;                        
					}
					else					
                        first_active_gen = ge->gen;
				
				// Check for first inactive generation after an ongoing activation
				// stabilized flag is set to handle possible future reactivation
				if(first_active_gen >= 0 && ge->n_active == 0 && !stabilized)
				{
					stabilized = YES;
					stabilized_gen = ge->gen;
					stabilization_yielded = NO;
				}
				
				// Check for reactivation. This could be before the previous activation was accepted as a solution, or it could be a search for further solutions if
				// continue_after_accept is set. first_active_gen is reused to mean the first generation of the new activation
				if(stabilized && (ge->flags & DIFFERS_FROM_STABLE))
					if (ge->gen > max_reactivation_gen)
					{
						prune_reactivation_too_late++;
						return;
					}
					else
	                {
						first_active_gen = ge->gen;
						stabilized = NO;
						stabilization_yielded = NO;
					}
				
                PRINT_DIAGNOSTIC(("Checking generation %d, flags %x all %x first_active_gen %d n_active %d changed %d\n", 
                       ge->gen, ge->flags, all_gens, first_active_gen, ge->n_active, changed));

                if(ge->flags & FILTER_MISMATCH) {
                        PRINT_DIAGNOSTIC_PRUNE(("Didn't match filter\n"));
                        //dump(1, 0);
                        prune_filter++;
                        return;
                }

                if(ge->flags & IN_FORBIDDEN_REGION) {
                        PRINT_DIAGNOSTIC_PRUNE(("Hit forbidden region\n"));
                        prune_forbidden++;
                        return;
                }

				// Check if an ongoing activation has lasted too long without break
                if((first_active_gen >= 0) && (ge->gen >= first_active_gen + max_active_gens_in_a_row)) {
                	if(ge->n_active > 0) {
                    	PRINT_DIAGNOSTIC_PRUNE(("Activity after generation %d\n", first_active_gen + repair_interval));
                		prune_stayed_active_too_long++;
                        return;
                	}
                }

				// Check if all conditions for a solution are met
				if (stabilized && !stabilization_yielded)
				{
					int accept_gen = stabilized_gen + inactive_gens_at_accept - 1;
					
					if (active_plus_inactive_gens_at_accept)
						accept_gen = lowest_of (accept_gen, first_active_gen + active_plus_inactive_gens_at_accept - 1);
					
					accept_gen = highest_of (accept_gen, u_filter->n_gens - 1);
                	
					if (ge->gen >= accept_gen)
						stabilization_yielded = YES;
					
					if (ge->gen == accept_gen && ge->gen >= first_next_sol_gen)
					{
						bellman_found_solution (ge->gen);
						// dump (1, 0);
          	            	        
						if (continue_after_accept)
							first_next_sol_gen = ge->gen + 1;
						else
						{
							PRINT_DIAGNOSTIC_PRUNE (("found a solution\n"));
							prune_solution++;
							return;
						}
					}
				}
				
				// Check if we can stop looking for a solution in continue_after_accept mode
				if (ge->gen > max_reactivation_gen && stabilized && stabilization_yielded)
				{
					prune_no_continuation_found++;
					return;
				}
        	}

//		dump(1, 0);
		
		int allow_new_oncells = (ge->gen <= max_reactivation_gen || !stabilized);
        bellman_choose_cells(u, g, allow_new_oncells, ge->gen, first_next_sol_gen);
}

#define TRY(cdx, cdy)                                                   \
        if(tile_get_cell(t->prev, x + cdx, y + cdy) == UNKNOWN_STABLE && validate_xy_for_symmetry(x + cdx, y + cdy) == YES) { \
                dx = cdx;                                               \
                dy = cdy;                                               \
                goto found;                                             \
        }

static int validate_xy_for_symmetry(int x, int y)
{
	switch(symmetry_type) {
	case NONE:
		   return YES;
	case HORIZ:
			if(y >= symmetry_ofs - y)
				return YES;
			else
				return NO;
			
	case VERT:
			if(x >= symmetry_ofs - x)
				return YES;
			else
				return NO;
	}
}

static int xy_symmetry(int x, int y, int* mirrorx_arr, int* mirrory_arr)
{
	mirrorx_arr[0] = x;
	mirrory_arr[0] = y;
		
	switch(symmetry_type) {
		case NONE:
			return 1;
		case HORIZ:
			if(y == symmetry_ofs - y)
				return 1;
			
			mirrorx_arr[1] = x;
			mirrory_arr[1] = symmetry_ofs - y;
		
			return 2;
			
		case VERT:
			if(x == symmetry_ofs - x)
				return 1;
			
			mirrorx_arr[1] =  symmetry_ofs - x;
			mirrory_arr[1] = y;
					
			return 2;

	}
}


typedef enum {COMPLEXITY_OK, COMPLEXITY_FAILED_LOCALLY, COMPLEXITY_FAILED_GLOBALLY} compl_result;

typedef struct
{
	int xon;
	int xoff;
	int yon;
	int yoff;
	int oncnt;
} compl_box;

static int init_compl_box (compl_box *cb)
{
	cb->xon = 0;
	cb->xoff = -1;
	cb->yon = 0;
	cb->yoff = -1;
	cb->oncnt = 0;
}

static int try_add_to_box (compl_box *cb, int x, int y, int compl_limit, int free_cells)
{
	int new_xon = cb->xon;
	int new_xoff = cb->xoff;
	int new_yon = cb->yon;
	int new_yoff = cb->yoff;
	int new_oncnt = cb->oncnt + 1;
	
	if (new_xon > new_xoff)
	{
		new_xon = x;
		new_xoff = x + 1;
	}
	else
	{
		if (new_xon > x)
			new_xon = x;
		if (new_xoff < x + 1)
			new_xoff = x + 1;
	}

	if (new_yon > new_yoff)
	{
		new_yon = y;
		new_yoff = y + 1;
	}
	else
	{
		if (new_yon > y)
			new_yon = y;
		if (new_yoff < y + 1)
			new_yoff = y + 1;
	}
	
	int compl = new_oncnt - free_cells;
	if (compl < 0)
		compl = 0;
            
	int bigside = new_xoff - new_xon;
	int shortside = new_yoff - new_yon;
	if (bigside < shortside)
	{
		bigside = new_yoff - new_yon;
		shortside = new_xoff - new_xon;
	}
	
	compl += (2 * bigside + shortside);
	
	if (compl <= compl_limit)
	{
		cb->xon = new_xon;
		cb->xoff = new_xoff;
		cb->yon = new_yon;
		cb->yoff = new_yoff;
		cb->oncnt = new_oncnt;
		return YES;
	}
	
	return NO;
}

static compl_result test_complexity ()
{
	compl_box local_box [MAX_MAX_LOCAL_AREAS];
	
	int box_ix;
	for (box_ix = 0; box_ix < max_local_areas; box_ix++)
		init_compl_box (&local_box [box_ix]);

	int oncell_ix;
	for (oncell_ix = 0; oncell_ix < onlist_cnt; oncell_ix++)
	{
		for (box_ix = 0; box_ix < max_local_areas; box_ix++)
			if (try_add_to_box (&local_box [box_ix], onlist_x [oncell_ix], onlist_y [oncell_ix], max_local_complexity, LOCAL_COMPLEXITY_FREE_CELLS))
				break;
		
		if (box_ix == max_local_areas)
			return COMPLEXITY_FAILED_LOCALLY;
	}

	compl_box global_box;
	init_compl_box (&global_box);

	for (oncell_ix = 0; oncell_ix < onlist_cnt; oncell_ix++)
		if (!try_add_to_box (&global_box, onlist_x [oncell_ix], onlist_y [oncell_ix], max_global_complexity, GLOBAL_COMPLEXITY_FREE_CELLS))
			return COMPLEXITY_FAILED_GLOBALLY;
	
	return COMPLEXITY_OK;
}


static void bellman_choose_cells (universe *u, generation *g, int allow_new_oncells, int first_gen_with_unknown_cells, int first_next_sol_gen)
{
        // Look for a tile with some unknown cells.

        g = u_evolving->first;
        
        tile *t;
        do {
                for(t = g->all_first; 
                    t && !(t->flags & HAS_UNKNOWN_CELLS);
                    t = t->all_next)
                        ;
                if(!t) g = g->next;
        } while(g && !t);

        if(!g)
		{
			// We got all the way to the end of the pattern. This should not happen anymore - all
			// solutions should be found before here and anything else should already be pruned
			
			fprintf (stderr, "Error: max generations reached without a solution or prune");
			assert (0);
        }

        // Find an unknown successor cell that's in the neighbourhood
        // of an unknown-stable predecessor cell.

        assert(t->prev);
        //generation_evolve(g, bellman_evolve);
        //printf("Generation %d has unknown cells\n", g->gen);
        int x, y, dx = 2, dy = 2;

        // Look for direct predecessors first ...

        for(y=0; y<TILE_HEIGHT; y++) {
                TILE_WORD is_unk = 0;
                is_unk = t->bit1[y] & ~t->bit0[y];
                if(is_unk) {
                        for(x = 0; x < TILE_WIDTH; x++) {
                                	
								if((is_unk >> x) & 1) {
                                        assert(tile_get_cell(t, x, y) == UNKNOWN);
                                        // Now look for an unknown-stable cell near it.
                                        if((x == 0) || (x == TILE_WIDTH-1) || (y == 0) || (y == TILE_HEIGHT-1)) {
                                                printf("TODO: handle tile wrap! (%d, %d, %d)\n", g->gen, x, y);
                                                dump(1, 0);
                                                assert(0);
                                        }

                                        
                                        TRY(0, 0);
                                }
                        }

                }
        }

        // ... then orthogonally adjacent cells ...

        for(y=0; y<TILE_HEIGHT; y++) {
                TILE_WORD is_unk = 0;
                is_unk = t->bit1[y] & ~t->bit0[y];
                if(is_unk) {
                        for(x = 0; x < TILE_WIDTH; x++) {
								
                                if((is_unk >> x) & 1) {
                                        assert(tile_get_cell(t, x, y) == UNKNOWN);
                                        // Now look for an unknown-stable cell near it.
                                        if((x == 0) || (x == TILE_WIDTH-1) || (y == 0) || (y == TILE_HEIGHT-1)) {
                                                printf("TODO: handle tile wrap! (%d, %d, %d)\n", g->gen, x, y);
                                                dump(1, 0);
                                                assert(0);
                                        }

                                        
                                        TRY(1, 0);
                                        TRY(0, 1);
                                        TRY(-1, 0);
                                        TRY(0, -1);
                                }
                        }

                }
        }

        // ... then diagonally adjacent ones.

        for(y=0; y<TILE_HEIGHT; y++) {
                TILE_WORD is_unk = 0;
                is_unk = t->bit1[y] & ~t->bit0[y];
                if(is_unk) {
                        for(x = 0; x < TILE_WIDTH; x++) {
								
                                if((is_unk >> x) & 1) {
                                        assert(tile_get_cell(t, x, y) == UNKNOWN);
                                        // Now look for an unknown-stable cell near it.
                                        if((x == 0) || (x == TILE_WIDTH-1) || (y == 0) || (y == TILE_HEIGHT-1)) {
                                                printf("TODO: handle tile wrap! (%d, %d, %d)\n", g->gen, x, y);
                                                dump(1, 0);
                                                assert(0);
                                        }

                                        
                                        TRY(-1, -1);
                                        TRY(-1, 1);
                                        TRY(1, -1);
                                        TRY(1, 1);
                                }
                        }

                }
        }

        printf("Didn't find an unknown cell!\n");
        dump(1, 0);
        assert(0);
        return;

found:
	assert(tile_get_cell(t, x, y) == UNKNOWN);
	assert(tile_get_cell(t->prev, x+dx, y+dy) == UNKNOWN_STABLE);
	assert(tile_get_cell((tile *)t->auxdata, x+dx, y+dy) == UNKNOWN_STABLE);
	
	PRINT_DIAGNOSTIC_RECURSE(("Generation %d, unknown cell at (%d, %d, %d)\n", g->gen, g->gen + 1, x+dx, y+dy));
	assert(dx <= 1);
	assert(dy <= 1);
	x += dx;
	y += dy;

	int xmirror[8], ymirror[8], n_sym, i;
	
	n_sym = xy_symmetry(x, y, xmirror, ymirror);

	for(i = 0; i < n_sym; i++) {
		if(tile_get_cell(t->prev, xmirror[i], ymirror[i]) != UNKNOWN_STABLE) {
				fprintf(stderr, "Input region is asymmetric (%d,%d)=%d (%d,%d)=%d\n",
						x, y, tile_get_cell(t->prev, x, y),
						xmirror, ymirror, tile_get_cell(t->prev, xmirror, ymirror));
				exit(-1);
		}
	}
	
#if 0
//		tile_set_cell(t->prev, x, y, OFF);
//		tile_set_cell(t->auxdata, x, y, OFF);
//		g->prev->flags |= CHANGED;
//
//		PRINT_DIAGNOSTIC_RECURSE(("Recursing with (%d,%d) = OFF\n", x, y));
//		bellman_recurse(u, g->prev);
#endif
		
		// Recurse with the selected cell as ON
		if (allow_new_oncells)
		{
			if(onlist_cnt + n_sym <= max_added_static_oncells) {
    	        if (onlist_cnt + n_sym > ONLIST_SIZE)
				{
					printf("On-cell list overflow\n");
					assert(0);
				}
				
				for(i = 0; i < n_sym; i++)
				{
					onlist_x [onlist_cnt] = xmirror [i];
					onlist_y [onlist_cnt] = ymirror [i];
        	    	onlist_cnt++;
				}
			
				compl_result cr = test_complexity ();
				if (cr == COMPLEXITY_OK)
				{
					for(i = 0; i < n_sym; i++){
						tile_set_cell(t->prev,  xmirror[i], ymirror[i], ON);
						tile_set_cell((tile *)t->auxdata,  xmirror[i], ymirror[i], ON);
					}
				
					g->prev->flags |= CHANGED;
				
					PRINT_DIAGNOSTIC_RECURSE(("Recursing with (%d,%d) = ON\n", x, y));
					bellman_recurse(u, g->prev, first_gen_with_unknown_cells, first_next_sol_gen);
				}
	            else if (cr == COMPLEXITY_FAILED_LOCALLY)
    	        	prune_too_complex_locally++;
        	    else if (cr == COMPLEXITY_FAILED_GLOBALLY)
            		prune_too_complex_globally++;
				
	            onlist_cnt -= n_sym;
				
			} else { 
				PRINT_DIAGNOSTIC_PRUNE(("Too many live cells\n"));
				prune_too_many_added_oncells++;
			}
		}
		else
			prune_new_oncells_not_allowed++;
		
		// Recurse with the selected cell as OFF
		for(i = 0; i < n_sym; i++){
			tile_set_cell(t->prev,  xmirror[i], ymirror[i], OFF);
			tile_set_cell((tile *)t->auxdata,  xmirror[i], ymirror[i], OFF);
		}
		
		g->prev->flags |= CHANGED;

		PRINT_DIAGNOSTIC_RECURSE(("Recursing with (%d,%d) = OFF\n", x, y));
		bellman_recurse(u, g->prev, first_gen_with_unknown_cells, first_next_sol_gen);
		
		for(i = 0; i < n_sym; i++){
			tile_set_cell(t->prev,  xmirror[i], ymirror[i], UNKNOWN_STABLE);
			tile_set_cell((tile *)t->auxdata,  xmirror[i], ymirror[i], UNKNOWN_STABLE);
		}
		
		g->prev->flags |= CHANGED;
}

int main(int argc, char *argv[]) {

        enum {
                SEARCH,
                CLASSIFY
        } mode = SEARCH;
        int verbose = 0;

        u_static = universe_new(OFF);
        u_evolving = universe_new(OFF);
        u_forbidden = universe_new(OFF);
        u_filter = universe_new(UNKNOWN);

        int c;

        while((c = getopt(argc, argv, "cv")) != -1) switch(c) {
        case 'c':
                mode = CLASSIFY;
                break;

        case 'v': verbose++; break;
        }

        FILE *f = fopen(argv[optind], "r");
        if(!f) {
                perror(argv[optind]);
                return -1;
        }

        read_life105(f, read_cb, read_param_cb, NULL);

        fclose(f);
        
		if (!explicit_max_reactivation_gen)
			max_reactivation_gen = max_first_activation_gen + DEFAULT_MIN_EXTRA_GENS_TO_ALLOW_REACTIVATION;
		
		// Lowest possible value is 2, 0 means this alternative accept condition is disabled
		if (active_plus_inactive_gens_at_accept < 2)
			active_plus_inactive_gens_at_accept = 0;

        max_gens = max_reactivation_gen + max_active_gens_in_a_row + inactive_gens_at_accept;
		if (active_plus_inactive_gens_at_accept)
	        max_gens = lowest_of (max_gens, max_reactivation_gen + highest_of (max_active_gens_in_a_row + 1, active_plus_inactive_gens_at_accept));

		if(max_gens < (u_filter->n_gens + 1))
		{
			max_gens = u_filter->n_gens + 1;
		}
		
        universe_evolve_next(u_static);

        unsigned int i;
        int x, y;
        generation *g;
        tile *t, *tp;

        g = universe_find_generation(u_static, 0, 0);
        for(t = g->all_first; t; t = t->all_next) {
                tile *t2 = universe_find_tile(u_forbidden, 0, t->xpos, t->ypos, 0);
                if(t2) t->auxdata = t2;
        }

        for(i=0; i<max_gens; i++) {
                universe_evolve_next(u_evolving);

                g = universe_find_generation(u_evolving, i, 0);
                for(t = g->all_first; t; t = t->all_next) {
                        tile *t2 = universe_find_tile(u_static, 0, t->xpos, t->ypos, 0);
                        if(t2) t->auxdata = t2;

                        t2 = universe_find_tile(u_filter, g->gen + 1, t->xpos, t->ypos, 0);
                        if(t2) t->filter = t2;
                }
        }

        /* set auxdata in the final generation: */
        g = universe_find_generation(u_evolving, i, 0);
        for(t = g->all_first; t; t = t->all_next) {
                tile *t2 = universe_find_tile(u_static, 0, t->xpos, t->ypos, 0);
                if(t2) t->auxdata = t2;
                t->filter = NULL;
        }
		
        bellman_evolve_generations(u_evolving->first, max_gens);

        int ac_first, ac_last;
        uint32_t klass;

        switch(mode) {
        case SEARCH:
				fprintf (stderr, "=== Bellman_szlim, v0.70 ===\n");
				if (!verify_static_is_stable ())
				{
					fprintf (stderr, "Catalysts in input file are not stable!\n");
					exit (-1);
				}
		
                printf("  Starting search, max generations = %d\n", max_gens);
                bellman_choose_cells(u_evolving, u_evolving->first, YES, 0, 0);

                print_prune_counters(YES);
                break;

        case CLASSIFY:

                if(verbose > 0) {
                        if(verbose > 1)
                                dump(1, 0);
                        for(g = u_evolving->first; g; g = g->next) {
                                printf("Generation %d: %x: %s\n", g->gen, g->flags, flag2str(g->flags));
                        }
                        
                }

                // print the history
                int in_interaction = 0, interaction_nr = 0;

                for(g = u_evolving->first->next; g; g = g->next) {
                        if(!(g->flags & IS_LIVE)) {
                                printf("log: g%d: died out\n", g->gen);
                                break;
                        }
                        if(g->flags & HAS_UNKNOWN_CELLS) {
                                printf("log: g%d: became undetermined\n", g->gen);
                                break;
                        }
                        if(!(g->flags & DIFFERS_FROM_PREVIOUS)) {
                                printf("log: g%d: became stable\n", g->gen);
                                break;
                        }
                        if(!(g->flags & DIFFERS_FROM_2PREV)) {
                                printf("log: g%d: became period 2\n", g->gen);
                                break;
                        }

                        if(!in_interaction) {
                                if(g->flags & DIFFERS_FROM_STABLE) {
                                        interaction_nr++;
                                        in_interaction = 1;
                                        printf("log: g%d: interaction %d begins\n", 
                                               g->gen, interaction_nr);
                                }
                        } else {
                                if(!(g->flags & DIFFERS_FROM_STABLE)) {
                                        in_interaction = 0;
                                        printf("log: g%d: interaction %d ends\n", 
                                               g->gen, interaction_nr);
                                }
                        }
                }

                // find the first active generation
                for(g = u_evolving->first; g && !(g->flags & DIFFERS_FROM_STABLE); g = g->next)
                        ;
                generation *g_last;

                if(!g) {
                        klass = 0;
                        goto done;
                }
                ac_first = g->gen;
                if(verbose > 0)
                        printf("First active generation: %d\n", ac_first);

                // find the generation after the last active generation
                g_last = g;
                for(; g; g = g->next) {
                        if(g->flags & DIFFERS_FROM_STABLE)
                                g_last = g;
                }

                if(!g_last) {
                        klass = 1;
                        goto done;
                }

                g = g_last->next ? g_last->next : g_last;
                ac_last = g->gen;
                if(verbose > 0)
                        printf("Last active generation: %d\n", ac_last);

                klass = (2 * ac_first) + (3 * ac_last);

                // The catalyst has returned to its stable state. Any
                // remaining differences are the generated spark.

                // We calculate a hash for each tile independently,
                // and sum them; this way the result is independent of
                // the order in which we traverse the tiles.

                for(t = g->all_first; t; t = t->all_next) {
                        uint32_t hash = 1;
                        tp = universe_find_tile(u_static, 0, t->xpos, t->ypos, 1);
                        for(y=0; y<TILE_HEIGHT; y++) for(x=0; x<TILE_WIDTH; x++) {
                                cellvalue t1 = tile_get_cell(t, x, y);
                                cellvalue t2 = tile_get_cell(tp, x, y);
                                if(t1 != t2) {
                                        hash = (hash ^ t1) * 0xabcdef13;
                                        hash = (hash ^ t2) * 0xabcdef13;
                                        hash = (hash ^ x) * 0xabcdef13;
                                        hash = (hash ^ y) * 0xabcdef13;
                                }
                        }
                        klass += hash;
                }
        done:
                printf("hash: %08x\n", klass);
                break;
        }
        return 0;
}
