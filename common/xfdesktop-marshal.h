
#ifndef __xfdesktop_marshal_MARSHAL_H__
#define __xfdesktop_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* BOOLEAN:VOID (xfdesktop-marshal.list:1) */
G_GNUC_INTERNAL void xfdesktop_marshal_BOOLEAN__VOID (GClosure     *closure,
                                                      GValue       *return_value,
                                                      guint         n_param_values,
                                                      const GValue *param_values,
                                                      gpointer      invocation_hint,
                                                      gpointer      marshal_data);

/* BOOLEAN:ENUM,INT (xfdesktop-marshal.list:2) */
G_GNUC_INTERNAL void xfdesktop_marshal_BOOLEAN__ENUM_INT (GClosure     *closure,
                                                          GValue       *return_value,
                                                          guint         n_param_values,
                                                          const GValue *param_values,
                                                          gpointer      invocation_hint,
                                                          gpointer      marshal_data);

/* VOID:UINT,BOXED (xfdesktop-marshal.list:3) */
G_GNUC_INTERNAL void xfdesktop_marshal_VOID__UINT_BOXED (GClosure     *closure,
                                                         GValue       *return_value,
                                                         guint         n_param_values,
                                                         const GValue *param_values,
                                                         gpointer      invocation_hint,
                                                         gpointer      marshal_data);

/* VOID:STRING,STRING (xfdesktop-marshal.list:4) */
G_GNUC_INTERNAL void xfdesktop_marshal_VOID__STRING_STRING (GClosure     *closure,
                                                            GValue       *return_value,
                                                            guint         n_param_values,
                                                            const GValue *param_values,
                                                            gpointer      invocation_hint,
                                                            gpointer      marshal_data);

G_END_DECLS

#endif /* __xfdesktop_marshal_MARSHAL_H__ */

