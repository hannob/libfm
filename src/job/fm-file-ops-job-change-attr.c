/*
 *      fm-file-ops-job-change-attr.c
 *      
 *      Copyright 2009 PCMan <pcman.tw@gmail.com>
 *      
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *      
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include "fm-file-ops-job-change-attr.h"

static const char query[] =  G_FILE_ATTRIBUTE_STANDARD_TYPE","
                               G_FILE_ATTRIBUTE_STANDARD_NAME","
                               G_FILE_ATTRIBUTE_UNIX_GID","
                               G_FILE_ATTRIBUTE_UNIX_UID","
                               G_FILE_ATTRIBUTE_UNIX_MODE","
                               G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME;

gboolean fm_file_ops_job_change_attr_file(FmFileOpsJob* job, GFile* gf, GFileInfo* inf)
{
    GError* err = NULL;
    GCancellable* cancellable = FM_JOB(job)->cancellable;
    GFileInfo* _inf = NULL;
    GFileType type;
    gboolean ret = TRUE;

    /* FIXME: need better error handling.
     * Some errors are recoverable or can be skipped. */

	if( !inf)
	{
		_inf = inf = g_file_query_info(gf, query,
							G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
							cancellable, &err);
        if(!_inf)
        {
            g_debug(err->message);
            fm_job_emit_error(job, err, FALSE);
            g_error_free(err);
		    return FALSE;
        }
	}

    type = g_file_info_get_file_type(inf);

    /* change owner */
    if( job->uid != -1 && !g_file_set_attribute_uint32(gf, G_FILE_ATTRIBUTE_UNIX_UID,
                                                  job->uid, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                  cancellable, &err) )
    {
        fm_job_emit_error(job, err, FALSE);
        g_error_free(err);
        return FALSE;
    }

    /* change group */
    if( job->gid != -1 && !g_file_set_attribute_uint32(gf, G_FILE_ATTRIBUTE_UNIX_GID,
                                                  job->gid, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                  cancellable, &err) )
    {
        fm_job_emit_error(job, err, FALSE);
        g_error_free(err);
        return FALSE;
    }

    /* change mode */
    if( job->new_mode_mask )
    {
        guint32 mode = g_file_info_get_attribute_uint32(inf, G_FILE_ATTRIBUTE_UNIX_MODE);
        mode &= ~job->new_mode_mask;
        mode |= (job->new_mode & job->new_mode_mask);

        /* FIXME: this behavior should be optionally */
        /* treat dirs with 'r' as 'rx' */
        if(type == G_FILE_TYPE_DIRECTORY)
        {
            if(mode & S_IRUSR)
                mode |= S_IXUSR;
            if(mode & S_IRGRP)
                mode |= S_IXGRP;
            if(mode & S_IROTH)
                mode |= S_IXOTH;
        }

        /* new mode */
        if( !g_file_set_attribute_uint32(gf, G_FILE_ATTRIBUTE_UNIX_MODE,
                                         mode, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                         cancellable, &err) )
        {
            fm_job_emit_error(job, err, FALSE);
            g_error_free(err);
            return FALSE;
        }
    }

    if(_inf)
    	g_object_unref(_inf);

    if(job->recursive && type == G_FILE_TYPE_DIRECTORY)
    {
		GFileEnumerator* enu = g_file_enumerate_children(gf, query,
									G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
									cancellable, &err);
        if(!enu)
        {
            fm_job_emit_error(job, err, FALSE);
            g_error_free(err);
		    g_object_unref(enu);
		    return FALSE;
        }

		while( ! FM_JOB(job)->cancel )
		{
			inf = g_file_enumerator_next_file(enu, cancellable, &err);
			if(inf)
			{
				GFile* sub = g_file_get_child(gf, g_file_info_get_name(inf));
				ret = fm_file_ops_job_change_attr_file(job, sub, inf); /* FIXME: error handling? */
				g_object_unref(sub);
				g_object_unref(inf);
                if(!ret)
                    break;
			}
			else
			{
                if(err)
                {
                    fm_job_emit_error(job, err, FALSE);
                    g_error_free(err);
                    ret = FALSE;
                    break;
                }
                else /* EOF */
                    break;
			}
		}
		g_object_unref(enu);
    }
    return ret;
}

gboolean fm_file_ops_job_change_attr_run(FmFileOpsJob* job)
{
	GList* l;
	/* prepare the job, count total work needed with FmDeepCountJob */
    if(job->recursive)
    {
        FmDeepCountJob* dc = fm_deep_count_job_new(job->srcs, FM_DC_JOB_DEFAULT);
        fm_job_run_sync(dc);
        job->total = dc->count;
        g_object_unref(dc);
    }
    else
        job->total = fm_list_get_length(job->srcs);

	g_debug("total number of files to change attribute: %llu", job->total);

	l = fm_list_peek_head_link(job->srcs);
	for(; !FM_JOB(job)->cancel && l;l=l->next)
	{
		GFile* src = fm_path_to_gfile((FmPath*)l->data);
		gboolean ret = fm_file_ops_job_change_attr_file(job, src, NULL);
		g_object_unref(src);
		if(!ret) /* error! */
            return FALSE;
	}
	return TRUE;
}
