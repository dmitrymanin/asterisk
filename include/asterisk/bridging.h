/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2009, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Channel Bridging API
 * \author Joshua Colp <jcolp@digium.com>
 * \ref AstBridging
 */

/*!
 * \page AstBridging Channel Bridging API
 *
 * The purpose of this API is to provide an easy and flexible way to bridge
 * channels of different technologies with different features.
 *
 * Bridging technologies provide the mechanism that do the actual handling
 * of frames between channels. They provide capability information, codec information,
 * and preference value to assist the bridging core in choosing a bridging technology when
 * creating a bridge. Different bridges may use different bridging technologies based on needs
 * but once chosen they all operate under the same premise; they receive frames and send frames.
 *
 * Bridges are a combination of bridging technology, channels, and features. A
 * developer creates a new bridge based on what they are currently expecting to do
 * with it or what they will do with it in the future. The bridging core determines what
 * available bridging technology will best fit the requirements and creates a new bridge.
 * Once created, channels can be added to the bridge in a blocking or non-blocking fashion.
 *
 * Features are such things as channel muting or DTMF based features such as attended transfer,
 * blind transfer, and hangup. Feature information must be set at the most granular level, on
 * the channel. While you can use features on a global scope the presence of a feature structure
 * on the channel will override the global scope. An example would be having the bridge muted
 * at global scope and attended transfer enabled on a channel. Since the channel itself is not muted
 * it would be able to speak.
 *
 * Feature hooks allow a developer to tell the bridging core that when a DTMF string
 * is received from a channel a callback should be called in their application. For
 * example, a conference bridge application may want to provide an IVR to control various
 * settings on the conference bridge. This can be accomplished by attaching a feature hook
 * that calls an IVR function when a DTMF string is entered.
 *
 */

#ifndef _ASTERISK_BRIDGING_H
#define _ASTERISK_BRIDGING_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/bridging_features.h"
#include "asterisk/dsp.h"

/*! \brief Capabilities for a bridge technology */
enum ast_bridge_capability {
	/*! Bridge should natively bridge two channels if possible */
	AST_BRIDGE_CAPABILITY_NATIVE = (1 << 1),
	/*! Bridge is only capable of mixing 2 channels */
	AST_BRIDGE_CAPABILITY_1TO1MIX = (1 << 2),
	/*! Bridge is capable of mixing 2 or more channels */
	AST_BRIDGE_CAPABILITY_MULTIMIX = (1 << 3),
	/*! Bridge should run using the multithreaded model */
	AST_BRIDGE_CAPABILITY_MULTITHREADED = (1 << 4),
	/*! Bridge should run a central bridge thread */
	AST_BRIDGE_CAPABILITY_THREAD = (1 << 5),
	/*! Bridge technology can do video mixing (or something along those lines) */
	AST_BRIDGE_CAPABILITY_VIDEO = (1 << 6),
	/*! Bridge technology can optimize things based on who is talking */
	AST_BRIDGE_CAPABILITY_OPTIMIZE = (1 << 7),
};

/*! \brief State information about a bridged channel */
enum ast_bridge_channel_state {
	/*! Waiting for a signal (Channel in the bridge) */
	AST_BRIDGE_CHANNEL_STATE_WAIT = 0,
	/*! Bridged channel was forced out and should be hung up (Bridge may dissolve.) */
	AST_BRIDGE_CHANNEL_STATE_END,
	/*! Bridged channel was forced out and should be hung up */
	AST_BRIDGE_CHANNEL_STATE_HANGUP,
	/*! Bridged channel was ast_bridge_depart() from the bridge without being hung up */
	AST_BRIDGE_CHANNEL_STATE_DEPART,
	/*! Bridged channel was ast_bridge_depart() from the bridge during AST_BRIDGE_CHANNEL_STATE_END */
	AST_BRIDGE_CHANNEL_STATE_DEPART_END,
};

/*! \brief Return values for bridge technology write function */
enum ast_bridge_write_result {
	/*! Bridge technology wrote out frame fine */
	AST_BRIDGE_WRITE_SUCCESS = 0,
	/*! Bridge technology attempted to write out the frame but failed */
	AST_BRIDGE_WRITE_FAILED,
	/*! Bridge technology does not support writing out a frame of this type */
	AST_BRIDGE_WRITE_UNSUPPORTED,
};

struct ast_bridge_technology;
struct ast_bridge;

/*!
 * \brief Structure specific to bridge technologies capable of
 * performing talking optimizations.
 */
struct ast_bridge_tech_optimizations {
	/*! The amount of time in ms that talking must be detected before
	 *  the dsp determines that talking has occurred */
	unsigned int talking_threshold;
	/*! The amount of time in ms that silence must be detected before
	 *  the dsp determines that talking has stopped */
	unsigned int silence_threshold;
	/*! Whether or not the bridging technology should drop audio
	 *  detected as silence from the mix. */
	unsigned int drop_silence:1;
};

/*!
 * \brief Structure that contains information regarding a channel in a bridge
 */
struct ast_bridge_channel {
	/*! Condition, used if we want to wake up a thread waiting on the bridged channel */
	ast_cond_t cond;
	/*! Current bridged channel state */
	enum ast_bridge_channel_state state;
	/*! Asterisk channel participating in the bridge */
	struct ast_channel *chan;
	/*! Asterisk channel we are swapping with (if swapping) */
	struct ast_channel *swap;
	/*! Bridge this channel is participating in */
	struct ast_bridge *bridge;
	/*! Private information unique to the bridge technology */
	void *bridge_pvt;
	/*! Thread handling the bridged channel */
	pthread_t thread;
	/*! Additional file descriptors to look at */
	int fds[4];
	/*! Bit to indicate whether the channel is suspended from the bridge or not */
	unsigned int suspended:1;
	/*! TRUE if the imparted channel must wait for an explicit depart from the bridge to reclaim the channel. */
	unsigned int depart_wait:1;
	/*! Features structure for features that are specific to this channel */
	struct ast_bridge_features *features;
	/*! Technology optimization parameters used by bridging technologies capable of
	 *  optimizing based upon talk detection. */
	struct ast_bridge_tech_optimizations tech_args;
	/*! Call ID associated with bridge channel */
	struct ast_callid *callid;
	/*! Linked list information */
	AST_LIST_ENTRY(ast_bridge_channel) entry;
	/*! Queue of actions to perform on the channel. */
	AST_LIST_HEAD_NOLOCK(, ast_frame) action_queue;
};

enum ast_bridge_action_type {
	/*! Bridged channel is to detect a feature hook */
	AST_BRIDGE_ACTION_FEATURE,
	/*! Bridged channel is to act on an interval hook */
	AST_BRIDGE_ACTION_INTERVAL,
	/*! Bridged channel is to send a DTMF stream out */
	AST_BRIDGE_ACTION_DTMF_STREAM,
	/*! Bridged channel is to indicate talking start */
	AST_BRIDGE_ACTION_TALKING_START,
	/*! Bridged channel is to indicate talking stop */
	AST_BRIDGE_ACTION_TALKING_STOP,
};

enum ast_bridge_video_mode_type {
	/*! Video is not allowed in the bridge */
	AST_BRIDGE_VIDEO_MODE_NONE = 0,
	/*! A single user is picked as the only distributed of video across the bridge */
	AST_BRIDGE_VIDEO_MODE_SINGLE_SRC,
	/*! A single user's video feed is distributed to all bridge channels, but
	 *  that feed is automatically picked based on who is talking the most. */
	AST_BRIDGE_VIDEO_MODE_TALKER_SRC,
};

/*! This is used for both SINGLE_SRC mode to set what channel
 *  should be the current single video feed */
struct ast_bridge_video_single_src_data {
	/*! Only accept video coming from this channel */
	struct ast_channel *chan_vsrc;
};

/*! This is used for both SINGLE_SRC_TALKER mode to set what channel
 *  should be the current single video feed */
struct ast_bridge_video_talker_src_data {
	/*! Only accept video coming from this channel */
	struct ast_channel *chan_vsrc;
	int average_talking_energy;

	/*! Current talker see's this person */
	struct ast_channel *chan_old_vsrc;
};

struct ast_bridge_video_mode {
	enum ast_bridge_video_mode_type mode;
	/* Add data for all the video modes here. */
	union {
		struct ast_bridge_video_single_src_data single_src_data;
		struct ast_bridge_video_talker_src_data talker_src_data;
	} mode_data;
};

/*!
 * \brief Structure that contains information about a bridge
 */
struct ast_bridge {
	/*! Condition, used if we want to wake up the bridge thread. */
	ast_cond_t cond;
	/*! Number of channels participating in the bridge */
	int num;
	/*! The video mode this bridge is using */
	struct ast_bridge_video_mode video_mode;
	/*! The internal sample rate this bridge is mixed at when multiple channels are being mixed.
	 *  If this value is 0, the bridge technology may auto adjust the internal mixing rate. */
	unsigned int internal_sample_rate;
	/*! The mixing interval indicates how quickly the bridges internal mixing should occur
	 * for bridge technologies that mix audio. When set to 0, the bridge tech must choose a
	 * default interval for itself. */
	unsigned int internal_mixing_interval;
	/*! TRUE if the bridge thread is waiting on channels in the bridge array */
	unsigned int waiting:1;
	/*! TRUE if the bridge thread should stop */
	unsigned int stop:1;
	/*! TRUE if the bridge thread should refresh itself */
	unsigned int refresh:1;
	/*! TRUE if the bridge has been dissolved.  Any channel that now tries to join is immediately ejected. */
	unsigned int dissolved:1;
	/*! Bridge flags to tweak behavior */
	struct ast_flags feature_flags;
	/*! Bridge technology that is handling the bridge */
	struct ast_bridge_technology *technology;
	/*! Private information unique to the bridge technology */
	void *bridge_pvt;
	/*! Thread running the bridge */
	pthread_t thread;
	/*! Enabled features information */
	struct ast_bridge_features features;
	/*! Array of channels that the bridge thread is currently handling */
	struct ast_channel **array;
	/*! Number of channels in the above array */
	size_t array_num;
	/*! Number of channels the array can handle */
	size_t array_size;
	/*! Call ID associated with the bridge */
	struct ast_callid *callid;
	/*! Linked list of channels participating in the bridge */
	AST_LIST_HEAD_NOLOCK(, ast_bridge_channel) channels;
	/*! Linked list of channels removed from the bridge and waiting to be departed. */
	AST_LIST_HEAD_NOLOCK(, ast_bridge_channel) depart_wait;
};

/*!
 * \brief Create a new bridge
 *
 * \param capabilities The capabilities that we require to be used on the bridge
 * \param flags Flags that will alter the behavior of the bridge
 *
 * \retval a pointer to a new bridge on success
 * \retval NULL on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge *bridge;
 * bridge = ast_bridge_new(AST_BRIDGE_CAPABILITY_1TO1MIX, AST_BRIDGE_FLAG_DISSOLVE_HANGUP);
 * \endcode
 *
 * This creates a simple two party bridge that will be destroyed once one of
 * the channels hangs up.
 */
struct ast_bridge *ast_bridge_new(uint32_t capabilities, int flags);

/*!
 * \brief Lock the bridge.
 *
 * \param bridge Bridge to lock
 *
 * \return Nothing
 */
#define ast_bridge_lock(bridge)	_ast_bridge_lock(bridge, __FILE__, __PRETTY_FUNCTION__, __LINE__, #bridge)
static inline void _ast_bridge_lock(struct ast_bridge *bridge, const char *file, const char *function, int line, const char *var)
{
	__ao2_lock(bridge, AO2_LOCK_REQ_MUTEX, file, function, line, var);
}

/*!
 * \brief Unlock the bridge.
 *
 * \param bridge Bridge to unlock
 *
 * \return Nothing
 */
#define ast_bridge_unlock(bridge)	_ast_bridge_unlock(bridge, __FILE__, __PRETTY_FUNCTION__, __LINE__, #bridge)
static inline void _ast_bridge_unlock(struct ast_bridge *bridge, const char *file, const char *function, int line, const char *var)
{
	__ao2_unlock(bridge, file, function, line, var);
}

/*!
 * \brief See if it is possible to create a bridge
 *
 * \param capabilities The capabilities that the bridge will use
 *
 * \retval 1 if possible
 * \retval 0 if not possible
 *
 * Example usage:
 *
 * \code
 * int possible = ast_bridge_check(AST_BRIDGE_CAPABILITY_1TO1MIX);
 * \endcode
 *
 * This sees if it is possible to create a bridge capable of bridging two channels
 * together.
 */
int ast_bridge_check(uint32_t capabilities);

/*!
 * \brief Destroy a bridge
 *
 * \param bridge Bridge to destroy
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_destroy(bridge);
 * \endcode
 *
 * This destroys a bridge that was previously created using ast_bridge_new.
 */
int ast_bridge_destroy(struct ast_bridge *bridge);

/*!
 * \brief Join (blocking) a channel to a bridge
 *
 * \param bridge Bridge to join
 * \param chan Channel to join
 * \param swap Channel to swap out if swapping
 * \param features Bridge features structure
 * \param tech_args Optional Bridging tech optimization parameters for this channel.
 * \param pass_reference TRUE if the bridge reference is being passed by the caller.
 *
 * \retval state that channel exited the bridge with
 *
 * Example usage:
 *
 * \code
 * ast_bridge_join(bridge, chan, NULL, NULL, NULL, 0);
 * \endcode
 *
 * This adds a channel pointed to by the chan pointer to the bridge pointed to by
 * the bridge pointer. This function will not return until the channel has been
 * removed from the bridge, swapped out for another channel, or has hung up.
 *
 * If this channel will be replacing another channel the other channel can be specified
 * in the swap parameter. The other channel will be thrown out of the bridge in an
 * atomic fashion.
 *
 * If channel specific features are enabled a pointer to the features structure
 * can be specified in the features parameter.
 */
enum ast_bridge_channel_state ast_bridge_join(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_channel *swap,
	struct ast_bridge_features *features,
	struct ast_bridge_tech_optimizations *tech_args,
	int pass_reference);

/*!
 * \brief Impart (non-blocking) a channel onto a bridge
 *
 * \param bridge Bridge to impart on
 * \param chan Channel to impart
 * \param swap Channel to swap out if swapping.  NULL if not swapping.
 * \param features Bridge features structure. Must be NULL or obtained by ast_bridge_features_new().
 * \param independent TRUE if caller does not want to reclaim the channel using ast_bridge_depart().
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_impart(bridge, chan, NULL, NULL, 0);
 * \endcode
 *
 * \details
 * This adds a channel pointed to by the chan pointer to the
 * bridge pointed to by the bridge pointer.  This function will
 * return immediately and will not wait until the channel is no
 * longer part of the bridge.
 *
 * If this channel will be replacing another channel the other
 * channel can be specified in the swap parameter.  The other
 * channel will be thrown out of the bridge in an atomic
 * fashion.
 *
 * If channel specific features are enabled, a pointer to the
 * features structure can be specified in the features
 * parameter.
 *
 * \note If you impart a channel as not independent you MUST
 * ast_bridge_depart() the channel.  The bridge channel thread
 * is created join-able.  The implication is that the channel is
 * special and is not intended to be moved to another bridge.
 *
 * \note If you impart a channel as independent you must not
 * ast_bridge_depart() the channel.  The bridge channel thread
 * is created non-join-able.  The channel must be treated as if
 * it were placed into the bridge by ast_bridge_join().
 * Channels placed into a bridge by ast_bridge_join() are
 * removed by a third party using ast_bridge_remove().
 */
int ast_bridge_impart(struct ast_bridge *bridge, struct ast_channel *chan, struct ast_channel *swap, struct ast_bridge_features *features, int independent);

/*!
 * \brief Depart a channel from a bridge
 *
 * \param bridge Bridge to depart from
 * \param chan Channel to depart
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_depart(bridge, chan);
 * \endcode
 *
 * This removes the channel pointed to by the chan pointer from the bridge
 * pointed to by the bridge pointer and gives control to the calling thread.
 * This does not hang up the channel.
 *
 * \note This API call can only be used on channels that were added to the bridge
 *       using the ast_bridge_impart API call with the independent flag FALSE.
 */
int ast_bridge_depart(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Remove a channel from a bridge
 *
 * \param bridge Bridge that the channel is to be removed from
 * \param chan Channel to remove
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_remove(bridge, chan);
 * \endcode
 *
 * This removes the channel pointed to by the chan pointer from the bridge
 * pointed to by the bridge pointer and requests that it be hung up. Control
 * over the channel will NOT be given to the calling thread.
 *
 * \note This API call can be used on channels that were added to the bridge
 *       using both ast_bridge_join and ast_bridge_impart.
 */
int ast_bridge_remove(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Merge two bridges together
 *
 * \param bridge0 First bridge
 * \param bridge1 Second bridge
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_merge(bridge0, bridge1);
 * \endcode
 *
 * This merges the bridge pointed to by bridge1 with the bridge pointed to by bridge0.
 * In reality all of the channels in bridge1 are simply moved to bridge0.
 *
 * \note The second bridge specified is not destroyed when this operation is
 *       completed.
 */
int ast_bridge_merge(struct ast_bridge *bridge0, struct ast_bridge *bridge1);

/*!
 * \brief Suspend a channel temporarily from a bridge
 *
 * \param bridge Bridge to suspend the channel from
 * \param chan Channel to suspend
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_suspend(bridge, chan);
 * \endcode
 *
 * This suspends the channel pointed to by chan from the bridge pointed to by bridge temporarily.
 * Control of the channel is given to the calling thread. This differs from ast_bridge_depart as
 * the channel will not be removed from the bridge.
 *
 * \note This API call can be used on channels that were added to the bridge
 *       using both ast_bridge_join and ast_bridge_impart.
 */
int ast_bridge_suspend(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Unsuspend a channel from a bridge
 *
 * \param bridge Bridge to unsuspend the channel from
 * \param chan Channel to unsuspend
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_unsuspend(bridge, chan);
 * \endcode
 *
 * This unsuspends the channel pointed to by chan from the bridge pointed to by bridge.
 * The bridge will go back to handling the channel once this function returns.
 *
 * \note You must not mess with the channel once this function returns.
 *       Doing so may result in bad things happening.
 */
int ast_bridge_unsuspend(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Change the state of a bridged channel without taking and releasing the bridge channel lock
 *
 * \param bridge_channel Channel to change the state on
 * \param new_state The new state to place the channel into
 *
 * \note Do not use this call outside the context of either interval hook callbacks or bridging core.
 *       This function assumes the bridge_channel is locked.
 */
void ast_bridge_change_state_nolock(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state);

/*!
 * \brief Change the state of a bridged channel
 *
 * \param bridge_channel Channel to change the state on
 * \param new_state The new state to place the channel into
 *
 * Example usage:
 *
 * \code
 * ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
 * \endcode
 *
 * This places the channel pointed to by bridge_channel into the state
 * AST_BRIDGE_CHANNEL_STATE_HANGUP.
 *
 * \note This API call is only meant to be used in feature hook callbacks to
 *       request the channel exit the bridge.
 */
void ast_bridge_change_state(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state);

/*!
 * \brief Put an action onto the specified bridge_channel.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to queue the action on.
 * \param action What to do.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note This API call is meant for internal bridging operations.
 * \note BUGBUG This may get moved.
 */
int ast_bridge_channel_queue_action(struct ast_bridge_channel *bridge_channel, struct ast_frame *action);

/*!
 * \brief Adjust the internal mixing sample rate of a bridge
 * used during multimix mode.
 *
 * \param bridge Channel to change the sample rate on.
 * \param sample_rate the sample rate to change to. If a
 *        value of 0 is passed here, the bridge will be free to pick
 *        what ever sample rate it chooses.
 *
 */
void ast_bridge_set_internal_sample_rate(struct ast_bridge *bridge, unsigned int sample_rate);

/*!
 * \brief Adjust the internal mixing interval of a bridge used
 * during multimix mode.
 *
 * \param bridge Channel to change the sample rate on.
 * \param mixing_interval the sample rate to change to.  If 0 is set
 * the bridge tech is free to choose any mixing interval it uses by default.
 */
void ast_bridge_set_mixing_interval(struct ast_bridge *bridge, unsigned int mixing_interval);

/*!
 * \brief Set a bridge to feed a single video source to all participants.
 */
void ast_bridge_set_single_src_video_mode(struct ast_bridge *bridge, struct ast_channel *video_src_chan);

/*!
 * \brief Set the bridge to pick the strongest talker supporting
 * video as the single source video feed
 */
void ast_bridge_set_talker_src_video_mode(struct ast_bridge *bridge);

/*!
 * \brief Update information about talker energy for talker src video mode.
 */
void ast_bridge_update_talker_src_video_mode(struct ast_bridge *bridge, struct ast_channel *chan, int talker_energy, int is_keyfame);

/*!
 * \brief Returns the number of video sources currently active in the bridge
 */
int ast_bridge_number_video_src(struct ast_bridge *bridge);

/*!
 * \brief Determine if a channel is a video src for the bridge
 *
 * \retval 0 Not a current video source of the bridge.
 * \retval None 0, is a video source of the bridge, The number
 *         returned represents the priority this video stream has
 *         on the bridge where 1 is the highest priority.
 */
int ast_bridge_is_video_src(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief remove a channel as a source of video for the bridge.
 */
void ast_bridge_remove_video_src(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Set channel to goto specific location after the bridge.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 * \param context Context to goto after bridge.
 * \param exten Exten to goto after bridge.
 * \param priority Priority to goto after bridge.
 *
 * \details Add a channel datastore to setup the goto location
 * when the channel leaves the bridge and run a PBX from there.
 *
 * \return Nothing
 */
void ast_after_bridge_set_goto(struct ast_channel *chan, const char *context, const char *exten, int priority);

/*!
 * \brief Set channel to run the h exten after the bridge.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 * \param context Context to goto after bridge.
 *
 * \details Add a channel datastore to setup the goto location
 * when the channel leaves the bridge and run a PBX from there.
 *
 * \return Nothing
 */
void ast_after_bridge_set_h(struct ast_channel *chan, const char *context);

/*!
 * \brief Set channel to go on in the dialplan after the bridge.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 * \param context Current context of the caller channel.
 * \param exten Current exten of the caller channel.
 * \param priority Current priority of the caller channel
 * \param parseable_goto User specified goto string from dialplan.
 *
 * \details Add a channel datastore to setup the goto location
 * when the channel leaves the bridge and run a PBX from there.
 *
 * If parseable_goto then use the given context/exten/priority
 *   as the relative position for the parseable_goto.
 * Else goto the given context/exten/priority+1.
 *
 * \return Nothing
 */
void ast_after_bridge_set_go_on(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *parseable_goto);

/*!
 * \brief Setup any after bridge goto location to begin execution.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 *
 * \details Pull off any after bridge goto location datastore and
 * setup for dialplan execution there.
 *
 * \retval 0 on success.  The goto location is set for a PBX to run it.
 * \retval non-zero on error or no goto location.
 *
 * \note If the after bridge goto is set to run an h exten it is
 * run here immediately.
 */
int ast_after_bridge_goto_setup(struct ast_channel *chan);

/*!
 * \brief Run a PBX on any after bridge goto location.
 * \since 12.0.0
 *
 * \param chan Channel to execute after bridge goto location.
 *
 * \details Pull off any after bridge goto location datastore
 * and run a PBX at that location.
 *
 * \note On return, the chan pointer is no longer valid because
 * the channel has hung up.
 *
 * \return Nothing
 */
void ast_after_bridge_goto_run(struct ast_channel *chan);

/*!
 * \brief Discard channel after bridge goto location.
 * \since 12.0.0
 *
 * \param chan Channel to discard after bridge goto location.
 *
 * \return Nothing
 */
void ast_after_bridge_goto_discard(struct ast_channel *chan);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_BRIDGING_H */
